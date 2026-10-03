# SETUP: QLoRA on a ROCm laptop (about 8 GB of VRAM, 24 GB of RAM)

Everything below about ROCm, the AMD GPU and bitsandbytes-on-HIP is
**[UNTESTED]**. The recipe was written and smoke-tested in a sandbox with **no
GPU**. What did run there is listed in [What was verified where](#what-was-verified-where).
Each version pin says where it was looked up.

## 0. Before Python

- A Linux install with the in-kernel `amdgpu` driver. Your user must be able
  to open `/dev/kfd` and `/dev/dri/renderD*`. On most distros that means
  being in the `render` and `video` groups:
  `sudo usermod -aG render,video $USER`, then log out and back in. **[UNTESTED]**
- AMD's official ROCm support list (`ROCm/rocm-install-on-linux`,
  `docs/reference/system-requirements.rst`, develop branch, read 2026-10-03)
  names desktop and workstation Radeons only (RX 7700 and up, RX 9060/9070,
  Radeon PRO). **No laptop GPU is on it.** Laptop RDNA2 and RDNA3 parts, and
  the 680M/780M iGPUs, run through the community override in step 3, or not at
  all.
- If your GPU is an iGPU (Radeon 680M or 780M), the "8 GB" is system RAM
  carved out in the BIOS (UMA frame buffer). Training then shares the 24 GB
  with the OS. **[UNTESTED]**
- Python 3.10 to 3.14. torch 2.14 requires `>=3.10` (PyPI `requires_python`,
  and pytorch `RELEASE.md`). The smoke test ran on 3.11.

## 1. A venv outside the repo

```sh
python3 -m venv ~/venvs/exs-ft
. ~/venvs/exs-ft/bin/activate
pip install --upgrade pip
```

## 2. torch, ROCm build

```sh
pip install torch==2.14.1 --index-url https://download.pytorch.org/whl/rocm7.14
```

Source: `pytorch/pytorch.github.io`, branch `site`, `published_versions.json`,
read 2026-10-03. Its `latest_stable` is 2.14.1 and its Linux pip command for
ROCm uses `.../whl/rocm7.14`. `pytorch/pytorch` `RELEASE.md` lists ROCm 7.14 as
the stable ROCm for 2.14. **[UNTESTED]**: the sandbox cannot reach
`download.pytorch.org`, so this wheel has not been installed.

The ROCm wheel is expected to carry the ROCm user-space libraries it needs, so a
system-wide ROCm install should not be required, only the kernel driver. This
is from general knowledge and was not verified here: **[UNTESTED]**.

Check that the GPU is visible. ROCm GPUs appear through torch's `cuda` API:

```sh
python -c "import torch; print(torch.__version__, torch.version.hip, torch.cuda.is_available()); \
print(torch.cuda.get_device_properties(0).name, torch.cuda.get_device_properties(0).gcnArchName)"
```

A good result has `torch.version.hip` not `None`, `is_available()` `True`, and
a `gfx…` arch name. If `hip` is `None`, you installed a CUDA or CPU build. Redo
step 2.

## 3. `HSA_OVERRIDE_GFX_VERSION` for consumer and laptop RDNA

Try without it first: run step 5. If the GPU is visible but every kernel fails
(`HIP error: invalid device function`, `no kernel image`, a hang or a segfault),
your card's exact `gfx` id is not compiled into the wheel. The community
workaround is to make the runtime report the nearest supported id:

| your arch (`gcnArchName`) | typical parts | try |
|---|---|---|
| gfx1031, gfx1032, gfx1034, gfx1035, gfx1036 (RDNA2) | RX 6600M/6700M/6800S, 680M iGPU | `export HSA_OVERRIDE_GFX_VERSION=10.3.0` |
| gfx1101, gfx1102, gfx1103 (RDNA3) | RX 7600S/7700S/7600M XT, 780M iGPU | `export HSA_OVERRIDE_GFX_VERSION=11.0.0` |

**[UNTESTED]**: this is widely used community practice and is not in AMD's
documentation of supported configurations. The arch-to-part mapping is from
general knowledge, not from a source read today. The override can also turn a
clean refusal into silent wrong numbers. `check_env.py` compares a 4-bit layer
against an fp32 reference for that reason. Put the `export` in the shell you
train from, or before each command.

## 4. The rest

```sh
pip install -r tools/finetune/train/requirements-rocm.txt
```

The pins and their sources are at the top of that file. The important one is
`bitsandbytes==0.50.2`. Its PyPI Linux x86_64 wheel contains
`libbitsandbytes_rocm64/70/71/714/715/72.so` (I listed the wheel's contents),
and its README lists AMD RDNA `gfx101X/103X/110X/115X/120X` as supported for
QLoRA 4-bit. No separate ROCm build of bitsandbytes should be needed. Whether it
picks the `rocm714` library under torch+rocm7.14 on your card is **[UNTESTED]**.

For `meta-llama/Llama-3.2-3B-Instruct` (gated), accept the licence on its
Hugging Face page, then run `hf auth login`. The default Qwen model needs no
login.

## 5. Probe, then a tiny real run

```sh
cd tools/finetune/train
python check_env.py          # exit 0: NF4 works; 1: fallback (a); 2: fallback (b)
python train_qlora.py --measure-only            # token lengths; downloads only the tokenizer
python train_qlora.py --smoke --out out/smoke   # 6 steps on the real 3B model, then a reload check
```

`check_env.py` runs a bf16/fp16 matmul, an NF4 double-quant `Linear4bit`
forward and backward against an fp32 reference, and a `PagedAdamW8bit` step on
the GPU. It prints a verdict. `--smoke` downloads the full base model
(Qwen2.5-Coder-3B-Instruct, a few GB) and exercises every step of the real run.

## Memory: an estimate

This is arithmetic, not a measurement. The architecture numbers for
Qwen2.5-Coder-3B (hidden 2048, 36 layers, MLP 11008, 2 KV heads, vocabulary
151,936, tied embeddings, about 3.1B parameters) are from memory of its model
card and were not re-read here.

| | GB |
|---|---|
| NF4 weights of the ~2.8B non-embedding parameters (0.5 byte each + double-quant constants) | ~1.5 |
| embedding (311M params). `prepare_model_for_kbit_training` upcasts it to fp32 | ~1.25 |
| LoRA r=16 on all 7 projections: ~30M params, fp32 weights + grads + 8-bit Adam state | ~0.3 |
| activations at batch 1. The longest record is 384 tokens (below), and logits over a 152k vocabulary dominate | ~0.5 |
| ROCm context, allocator slack | 0.5 to 1 |
| **total** | **~4 to 5** |

So the default should fit 8 GB with headroom. Batches are padded only to the
longest record in the batch, so `--max-seq-len 1024` costs nothing beyond the
real lengths. If you run out of memory anyway, use `--quant none` with the 1.5B
model (fallback a) before touching the hyperparameters. Plain-LoRA 1.5B in bf16
is about 3.1 GB of weights plus the same small overheads.

## Fallbacks, in order

**(a) bitsandbytes 4-bit fails on your card.** `check_env.py` exits 1, or
training dies inside `bitsandbytes`. Drop quantisation and the model size:

```sh
python train_qlora.py --quant none --model Qwen/Qwen2.5-Coder-1.5B-Instruct --out out/qwen1.5b-lora
```

The optimizer falls back to `adamw_torch` by itself if bitsandbytes does not
import. If it imports but the paged optimizer fails, add `--optim adamw_torch`.
`generate.py --adapter out/qwen1.5b-lora` picks up `--quant none` and the model
from the adapter's `run_config.json`. For the base-model generation, pass the
same `--model … --quant none` yourself.

If bf16 gives NaN or is very slow on RDNA2, add `--compute-dtype fp16`. If
fp16 overflows (non-finite loss, which stops the run before saving), use
`--compute-dtype fp32`. **[UNTESTED]** on any GPU.

**(b) The GPU is unusable.** `check_env.py` exits 2. A CPU smoke test is all
that is realistic: it checks the pipeline, not the model. A 3B model on CPU
for 3 epochs is many hours or more, and is not a recipe here.

```sh
python train_qlora.py --cpu --smoke --quant none --model Qwen/Qwen2.5-0.5B-Instruct --out out/cpu-smoke
python generate.py --cpu --prompts ../out/eval_prompts.jsonl --adapter out/cpu-smoke \
    --out out/cpu-smoke-gen.jsonl --limit 3
```

## What was verified where

Run in the sandbox (Linux x86_64, 4 CPUs, **no GPU**; torch 2.14.1 from PyPI,
running on CPU; transformers 5.18.0, peft 0.21.2, accelerate 1.15.0,
bitsandbytes 0.50.2). The Hugging Face hub was blocked there, so these runs
used a 2-layer **random-weight** Qwen2 model (`smoke_tiny_model.py`) built
around the real Qwen2.5 tokenizer files:

- assistant-only loss masking. Every one of the 563 records: unmasked labels
  decode exactly to the assistant text plus `<|im_end|>`, and padding is -100.
- `--quant none` and `--quant nf4` (bitsandbytes' CPU backend) training: loss
  finite. Over 64 steps on 8 records it falls from 11.93 to 11.35.
- the adapter saves, reloads, and reproduces the in-memory loss exactly.
- `generate.py` with and without an adapter, NF4 included, and resume after a
  torn last line.
- `check_env.py` on CPU.
- two identical CPU runs give byte-identical adapters.

Not run anywhere: ROCm, any AMD GPU, the bitsandbytes HIP kernels,
bf16/fp16 mixed precision on a GPU, `HSA_OVERRIDE_GFX_VERSION`, the real 3B,
1.5B or 0.5B weights, the Llama 3.2 chat template, memory use, and wall time.
