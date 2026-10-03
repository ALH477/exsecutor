# tools/finetune/train: a QLoRA recipe for datasets/finetune

This directory answers one question: **does fine-tuning a small coder model on
`datasets/finetune/` work at all?** It is sized for a laptop AMD GPU under ROCm
with about 8 GB of VRAM. Install steps, fallbacks and what is **[UNTESTED]**
are in [SETUP.md](SETUP.md).

| file | what |
|---|---|
| `train_qlora.py` | measures token lengths, then QLoRA (NF4) or LoRA with loss on the assistant tokens only. Saves the adapter and `run_config.json` |
| `generate.py` | greedy `{id, text}` generations from the base model or base + adapter. Resumable |
| `check_env.py` | probes torch/ROCm/bitsandbytes on your GPU and says which recipe applies |
| `smoke_tiny_model.py` | a test fixture: a random-weight 2-layer model around a real tokenizer |
| `requirements-rocm.txt` | pins, with where each was looked up |

Defaults: `Qwen/Qwen2.5-Coder-3B-Instruct` with NF4 and double quant. LoRA
r=16, alpha=32, dropout 0.05 on q/k/v/o/gate/up/down. Batch 1 with grad-accum
16, cosine lr 2e-4, warmup 3%, 3 epochs, seed 42, gradient checkpointing, and
`paged_adamw_8bit` when bitsandbytes works.

## Run order

From the repo root, inside the venv from SETUP.md:

```sh
# 1. setup: SETUP.md, then
python tools/finetune/train/check_env.py

# 2. train (reports token lengths first; an over-long record stops the run, nothing is truncated)
python tools/finetune/train/train_qlora.py --out tools/finetune/train/out/qwen3b-nf4

# 3. held-out prompts (the eval scripts' own step)
python tools/finetune/make_eval_prompts.py            # -> tools/finetune/out/eval_prompts.jsonl

# 4. generate: base, then tuned. Same model, quant and system prompt
python tools/finetune/train/generate.py --prompts tools/finetune/out/eval_prompts.jsonl \
    --out tools/finetune/out/gen-base.jsonl
python tools/finetune/train/generate.py --prompts tools/finetune/out/eval_prompts.jsonl \
    --adapter tools/finetune/train/out/qwen3b-nf4 --out tools/finetune/out/gen-tuned.jsonl

# 5. score with the real compiler (needs build/exsc and fasmg), then the paired comparison
python tools/finetune/score.py tools/finetune/out/gen-base.jsonl  --label base
python tools/finetune/score.py tools/finetune/out/gen-tuned.jsonl --label tuned
python tools/finetune/compare.py tools/finetune/out/gen-base.results.jsonl tools/finetune/out/gen-tuned.results.jsonl
```

The eval scripts in step 5 belong to `tools/finetune/` and are documented
there. Check their `--help` before relying on the paths above.

## Wall time: an ESTIMATE, not a measurement

Nothing here has run on a GPU. The training set is 507 records and 84.5k
tokens per epoch under the Qwen tokenizer, measured on the dataset as it
stood when this was written. That comes to 96 optimizer steps over 3 epochs. A 3B QLoRA step with gradient checkpointing on a laptop RDNA GPU
might process a few hundred to a thousand tokens a second. That puts
**training at roughly 20 minutes to 2 hours**, with an iGPU at the slow end or
beyond. Generation covers about 10 eval items, up to 512 new tokens each, per
model. At an assumed 5 to 20 tokens a second, that is **roughly 5 to 30
minutes per model**. Get your real numbers from the first few logged steps,
from `train_runtime` in `trainer_log.json`, and from generate.py's
per-item lines.

## Reading the result honestly

- **The held-out code set is tiny.** `make_eval_prompts.py` currently yields
  10 items: 8 write, 1 fix, 1 trace. That count comes from a run on this tree;
  rerun it to get yours. With this few items, "tuned passes 4, base passes 1"
  is not evidence by itself. Use `compare.py`'s paired result. Only discordant
  items count, and with d discordant items the smallest possible two-sided
  exact p is 2 x 0.5^d. So fewer than 6 discordant items can **never** reach
  p < 0.05.
- **Code passing is the only measured outcome.** `score.py` compiles and runs
  the generated programs. A tuned model that answers lexicon or concept
  questions better (it will, because 340 of 563 records are lexicon) is not
  evidence it writes better Exsecutor. Do not read falling `eval_loss` in
  `trainer_log.json` as code quality either. Most validation tokens are prose.
- **Most training code was type-checked, not run.** When this was written,
  the dataset's `manifest.json` counted 196 `exsc-check` records and 37
  `exsc-run`. A model can learn programs that type-check and still do the
  wrong thing.
- **The base model has never seen Exsecutor**, so it scoring about zero is
  expected. That gap shows the adapter taught the syntax. It does not show the
  model is a useful agent. Treat any "agent" use as a separate experiment.
- **Licence:** `datasets/finetune/README.md` says whether weights trained on
  this GPL material are bound by the GPL is unsettled. Ask before distributing
  an adapter.

## What the smoke test proves, and what it does not

SETUP.md lists what ran in the sandbox, which had no GPU, and what has not
run anywhere. In short, the masking, the loss, save/reload, generation and
resume were run on CPU with a random-weight model and the real tokenizer.
ROCm, the bitsandbytes HIP kernels, real weights, memory use and wall time are
**[UNTESTED]**.
