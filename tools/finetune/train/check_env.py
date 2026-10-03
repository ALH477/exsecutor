#!/usr/bin/env python3
# tools/finetune/train/check_env.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# Answers, by running things rather than by reading version strings:
#   1. is the GPU visible to torch (and is it a ROCm/HIP build)?
#   2. does a bf16 / fp16 matmul run on it?
#   3. does a bitsandbytes NF4 (double-quant) layer run forward AND backward
#      on it, with a LoRA-style trainable side path? (what QLoRA needs)
#   4. does the 8-bit paged optimizer take a step?
# The verdict line tells you which recipe in SETUP.md to use.
# ---------------------------------------------------------------------------
"""Probe torch / ROCm / bitsandbytes and print which SETUP.md recipe applies.

  python check_env.py            # probe the GPU (falls back to CPU if none)
  python check_env.py --device cpu
Exit status: 0 = NF4 QLoRA path works, 1 = use fallback (a) --quant none,
2 = no usable GPU, fallback (b) CPU smoke only.
"""

import argparse
import sys


def step(name, fn):
    try:
        out = fn()
        print("  ok    %s%s" % (name, (": " + out) if out else ""))
        return True
    except Exception as e:  # noqa: BLE001 -- every failure is a finding to print
        msg = str(e).strip().splitlines()
        print("  FAIL  %s: %s: %s" % (name, type(e).__name__, msg[0] if msg else ""))
        return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device", choices=("auto", "cuda", "cpu"), default="auto",
                    help="auto: the GPU if torch sees one (ROCm GPUs appear as 'cuda'), else cpu")
    a = ap.parse_args()
    try:
        import torch
    except ImportError:
        print("torch is not installed; see SETUP.md step 2")
        return 2
    print("torch %s  hip=%s  cuda=%s" % (torch.__version__, getattr(torch.version, "hip", None), torch.version.cuda))
    gpu = torch.cuda.is_available()
    print("torch.cuda.is_available() = %s  (ROCm GPUs are reported through the cuda API)" % gpu)
    dev = "cuda" if (a.device == "cuda" or (a.device == "auto" and gpu)) else "cpu"
    if dev == "cuda":
        if not gpu:
            print("no GPU visible")
            return 2
        p = torch.cuda.get_device_properties(0)
        print("device 0: %s  arch=%s  memory=%.2f GiB  bf16_supported=%s"
              % (p.name, getattr(p, "gcnArchName", "?"), p.total_memory / 2**30, torch.cuda.is_bf16_supported()))
    else:
        print("probing on CPU")

    def mm(dtype):
        def run():
            x = torch.randn(256, 256, device=dev, dtype=dtype)
            y = (x @ x).float()
            if not torch.isfinite(y).all():
                raise RuntimeError("non-finite result")
            if dev == "cuda":
                torch.cuda.synchronize()
            return ""
        return run

    ok_gpu = step("fp32 matmul", mm(torch.float32))
    if dev == "cuda" and not ok_gpu:
        print("VERDICT: the GPU is visible but cannot run a matmul -> fallback (b), CPU smoke only "
              "(check HSA_OVERRIDE_GFX_VERSION, SETUP.md step 3)")
        return 2
    step("bf16 matmul", mm(torch.bfloat16))
    step("fp16 matmul", mm(torch.float16))

    try:
        import bitsandbytes as bnb
        print("bitsandbytes %s" % bnb.__version__)
    except Exception as e:  # noqa: BLE001
        print("bitsandbytes does not import: %s: %s" % (type(e).__name__, e))
        print("VERDICT: fallback (a): --quant none with a smaller model (SETUP.md)")
        return 1 if dev == "cuda" else 2

    cdt = torch.bfloat16 if (dev == "cuda" and torch.cuda.is_bf16_supported()) else (
        torch.float16 if dev == "cuda" else torch.float32)

    def nf4():
        torch.manual_seed(0)
        base = torch.nn.Linear(512, 512, bias=False)
        q = bnb.nn.Linear4bit(512, 512, bias=False, compute_dtype=cdt, compress_statistics=True, quant_type="nf4")
        q.weight = bnb.nn.Params4bit(base.weight.data.clone(), requires_grad=False, compress_statistics=True,
                                     quant_type="nf4")
        q = q.to(dev)
        lora_a = torch.nn.Parameter(torch.randn(8, 512, device=dev) * 0.01)
        lora_b = torch.nn.Parameter(torch.zeros(512, 8, device=dev))
        x = torch.randn(4, 512, device=dev, dtype=cdt, requires_grad=False)
        y = q(x).float() + (x.float() @ lora_a.t()) @ lora_b.t()
        ref = x.float() @ base.weight.to(dev).float().t()
        err = ((y - ref).abs().mean() / ref.abs().mean()).item()
        y.pow(2).mean().backward()
        if lora_b.grad is None or not torch.isfinite(lora_b.grad).all():
            raise RuntimeError("no finite gradient through the 4-bit layer")
        if err > 0.2:
            raise RuntimeError("4-bit output too far from fp32 reference (rel err %.3f)" % err)
        return "rel err vs fp32 %.4f, grad finite" % err

    ok_nf4 = step("NF4 + double-quant Linear4bit forward/backward (%s)" % str(cdt).replace("torch.", ""), nf4)

    def paged():
        p = torch.nn.Parameter(torch.randn(4096, 8, device=dev))
        opt = bnb.optim.PagedAdamW8bit([p], lr=1e-3)
        p.grad = torch.randn_like(p)
        before = p.detach().clone()
        opt.step()
        if torch.equal(before, p.detach()):
            raise RuntimeError("parameter did not change")
        return ""

    ok_opt = step("PagedAdamW8bit step", paged)

    if dev == "cpu":
        print("VERDICT: CPU only. Fallback (b): train_qlora.py --smoke (see SETUP.md)")
        return 2
    if ok_nf4:
        print("VERDICT: the default QLoRA recipe should run (--quant nf4)%s"
              % ("" if ok_opt else "; pass --optim adamw_torch, the paged 8-bit optimizer failed"))
        return 0
    print("VERDICT: fallback (a): --quant none --model Qwen/Qwen2.5-Coder-1.5B-Instruct")
    return 1


if __name__ == "__main__":
    sys.exit(main())
