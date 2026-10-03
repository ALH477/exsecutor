#!/usr/bin/env python3
# tools/finetune/train/smoke_tiny_model.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DeMoD LLC.
# ---------------------------------------------------------------------------
# Test fixture, not a model: writes a 2-layer, randomly initialised Qwen2
# causal LM next to a REAL tokenizer, so train_qlora.py --smoke and
# generate.py can be exercised end to end (masking, loss, save, reload,
# generation, resume) with no weight download. Its generations are noise by
# construction; nothing about model quality can be read from them.
# ---------------------------------------------------------------------------
"""Build a tiny random-weight Qwen2 model dir around an existing tokenizer dir.

  python smoke_tiny_model.py --tokenizer DIR_WITH_tokenizer.json --out /tmp/tiny-qwen2

DIR must hold a Qwen2-family tokenizer (tokenizer.json, tokenizer_config.json
with a chat template). The weights are seeded (--seed) and the files written
are a pure function of the inputs.
"""

import argparse
import os
import shutil
import sys

TOKENIZER_FILES = ("tokenizer.json", "tokenizer_config.json", "vocab.json", "merges.txt",
                   "special_tokens_map.json", "added_tokens.json")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tokenizer", required=True, help="directory holding the tokenizer files")
    ap.add_argument("--out", required=True, help="output model directory")
    ap.add_argument("--seed", type=int, default=0, help="default: %(default)s")
    ap.add_argument("--hidden", type=int, default=64, help="hidden size (default: %(default)s)")
    ap.add_argument("--layers", type=int, default=2, help="default: %(default)s")
    a = ap.parse_args()

    import torch
    from transformers import AutoTokenizer, Qwen2Config, Qwen2ForCausalLM

    tok = AutoTokenizer.from_pretrained(a.tokenizer)
    if tok.chat_template is None:
        sys.exit("smoke_tiny_model: %s has no chat template" % a.tokenizer)
    torch.manual_seed(a.seed)
    cfg = Qwen2Config(vocab_size=max(len(tok), 151936), hidden_size=a.hidden, intermediate_size=2 * a.hidden,
                      num_hidden_layers=a.layers, num_attention_heads=4, num_key_value_heads=2,
                      max_position_embeddings=4096, tie_word_embeddings=True,
                      bos_token_id=tok.bos_token_id, eos_token_id=tok.eos_token_id,
                      pad_token_id=tok.pad_token_id)
    model = Qwen2ForCausalLM(cfg)
    os.makedirs(a.out, exist_ok=True)
    model.save_pretrained(a.out)
    copied = []
    for name in TOKENIZER_FILES:
        src = os.path.join(a.tokenizer, name)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(a.out, name))
            copied.append(name)
    n = sum(p.numel() for p in model.parameters())
    print("smoke_tiny_model: %d params (random, seed %d), tokenizer files %s -> %s"
          % (n, a.seed, ", ".join(copied), a.out))


if __name__ == "__main__":
    main()
