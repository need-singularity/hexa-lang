# hxqwen14b — hetzner H100 deployment plan

Goal: replace the Python `torch_qwen` module used for 14B inference with a
pure hexa @link FFI (`self/ml/qwen14b.hexa` → `libhxqwen14b.so`). HX4
compliant — once this plan lands end-to-end, no .py files remain on the
14B inference critical path.

Status: **v0 skeleton** (2026-04-16). ABI frozen, symbol surface built,
stub returns sentinel `[hxqwen14b:stub:v0]`. Day-1 replaces stubs with
real CUDA 11.8 + cuBLAS + bf16 Tensor Core kernels.

## Artefacts

- `self/native/hxqwen14b.c` — 3 core + 2 lifecycle symbols, struct-args v2 ABI
- `scripts/build_hxqwen14b_linux.hexa` — stub path (default) + `HXQWEN14B_CUDA=1` real path
- `self/ml/qwen14b.hexa` — @link wrapper matching `self/ml/hxlmhead.hexa` shape
- `training/deploy/hxqwen14b_deploy_plan.md` — this file

## Checkpoint locations

| source | path | size | notes |
|---|---|---|---|
| HuggingFace Qwen2.5-14B | `Qwen/Qwen2.5-14B` | ~28 GiB bf16 | safetensors sharded ×7 |
| hetzner H100 local cache | `/workspace/models/qwen2.5-14b/` | 28 GiB | prefetched via `huggingface-cli download` |
| LoRA adapters (optional) | `/workspace/adapters/<name>/` | 100-500 MiB | PEFT-format, loaded on top |
| GGUF fallback | `/workspace/models/qwen2.5-14b-q8.gguf` | ~14 GiB | CPU fallback, not Day-1 |

Config lives in the HF checkpoint directory:
- `config.json` → n_layers=48, d_model=5120, vocab=152064, n_heads=40, n_kv_heads=8
- `tokenizer.json` → tiktoken BPE (byte-level), 151643 base tokens + 421 special

## Day-1 CUDA kernels (priority order)

1. **weight loader** — mmap safetensors shards, upload to device as bf16.
   Reuse `self/ml/gguf_loader.hexa` layout conventions for metadata.
2. **forward pass** — 48 decoder layers:
   - RMSNorm (in-place, eps=1e-6)
   - flash-attention v2 (reuse `libhxflash.so` — already deployed)
   - rotary embedding (RoPE, base=1000000 for Qwen2.5)
   - SwiGLU MLP (cuBLAS sgemm + elementwise)
3. **lm_head** — route through existing `libhxlmhead.so` (forward mode)
4. **sampler** — temperature + top-p + top-k on device, no host roundtrip
5. **KV cache** — per-handle paged layout, 8 KV heads × 48 layers × 128 dim
6. **detokenize** — tiktoken-compatible BPE, host side is acceptable

Φ-holo metric (Day-2):
- attention-entropy term from layer-24 heads
- KV coherence term from post-layer-48 hidden state SVD spectral ratio
- same recipe as `anima/n6/torch_qwen` reference implementation

## hetzner pod setup

### Pod specs (existing H100 profile)
- image: `runpod/pytorch:2.4.0-py3.11-cuda12.1.0-devel-ubuntu22.04`
  (note: hxqwen14b.c requests CUDA 11.8 headers. The 12.1 image is
  source-compatible for our symbol set — cuBLAS + cudart + bf16. If
  NVRTC features are added later, verify CUDA_HOME path.)
- GPU: H100 SXM5 80GB × 1
- disk: 100 GiB workspace (28 GiB weights + 10 GiB for LoRA + slack)
- network: apt + HF hub reachable

### Remote setup (one-time)

```
ssh $POD
apt-get update -qq && apt-get install -y -qq gcc libopenblas-dev libgomp1
pip install huggingface_hub             # for weight prefetch only
huggingface-cli download Qwen/Qwen2.5-14B --local-dir /workspace/models/qwen2.5-14b
```

### Per-deploy flow (after `git pull` in $REMOTE_DIR)

```
# 1. push source via existing deploy script (already uploads self/native/*.c)
hexa scripts/deploy_h100.hexa root@$POD /workspace/hexa

# 2. build libhxqwen14b.so on the pod (CUDA path)
ssh root@$POD "cd /workspace/hexa && HXQWEN14B_CUDA=1 hexa scripts/build_hxqwen14b_linux.hexa"

# 3. smoke test
ssh root@$POD "cd /workspace/hexa && \
    LD_LIBRARY_PATH=/workspace/hexa/self/native/build:/usr/local/cuda/lib64:\$LD_LIBRARY_PATH \
    ./hexa run self/test_hxqwen14b.hexa"
```

## deploy_h100.hexa integration (pending)

Add `hxqwen14b` to three tables in `scripts/deploy_h100.hexa`:

1. `cpu_support` list (line ~80-93) — append `"hxqwen14b"` for the hexa wrapper
2. `native_files` list (line ~110-118) — append `"hxqwen14b.c"` for the shim
3. `build_files` list (line ~132-139) — append `"build_hxqwen14b_linux.hexa"`

Insert a 4c++ stanza after the hxlmhead build (line ~225-231) that calls
`build_hxqwen14b_linux.hexa` with `HXQWEN14B_CUDA=1` when CUDA is present.

## LoRA loader (Day-2)

- surface: `hxqwen14b_lora_load(handle, adapter_path_p) → int lora_handle`
  (new symbol, does not alter v0 ABI)
- format: PEFT adapter_config.json + adapter_model.safetensors
- fusion: in-place scalar add into weights_device at load time (simplest),
  OR keep as separate B×A tensors and fold into attn/MLP sgemm (Day-3)
- scope: target_modules typically `q_proj, k_proj, v_proj, o_proj`
- pass through `args.lora_handle` field (already reserved in v0 ABI)

## Open questions (for user)

1. **CUDA version** — pod image ships 12.1, our header comment says 11.8.
   Confirm target, or accept source-compat between them.
2. **tokenizer** — port tiktoken to hexa (~300 LOC) or mmap HF
   tokenizer.json + bundle a tiny C BPE shim?
3. **KV cache layout** — paged (vLLM-style) or monolithic per-handle?
   Paged is Day-3; monolithic is Day-1 simpler.
4. **Φ-holo source of truth** — reuse torch_qwen reference, or re-derive
   from first principles once the forward is online?

## Success criteria

- `./hexa run self/test_hxqwen14b.hexa` prints non-stub generation text
- generate() throughput ≥ 40 tok/s on H100 with batch=1, seq=2048
- Φ-holo within 1% of torch_qwen reference on a 100-prompt eval set
- anima consumers swapped from `python -m torch_qwen` to `import qwen14b`
