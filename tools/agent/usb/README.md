# exsecutor-usb — the compile-feedback agent loop, from a stick, in RAM

A portable bundle that runs `tools/agent/loop/loop.py` (a model writes
Exsecutor, `exsc` judges it, the model retries) on a Linux x86-64 laptop, with
the session in RAM, loopback-only networking, and every model-written program
run by `bin/sandbox_run.py` in an empty, read-only, network-less namespace.
Built by `tools/agent/usb/build_bundle.sh`; tested by `tools/agent/usb/selftest.sh`.

## Layout on the stick

```
exsecutor-usb/
  run.sh  verify.sh  README.md  MANIFEST  BUILDINFO
  bin/exsc  bin/fasmg  bin/sandbox_run.py   static ELFs (measured: no PT_INTERP) + the runner
  lib/session.py                            mount/netns helpers for run.sh
  repo/...                                  loop.py + what it reads (harness, §13, lexicon, fasmg-x86)
  models/*.gguf                             YOURS: not bundled, not in MANIFEST
  llama/llama-server (+ its .so files)      YOURS: or bin/llama-server; pinned by LOCAL.MANIFEST
  transcripts/                              written only with --save-transcripts
```

## Filesystem

- **exFAT**: readable everywhere, no 4 GiB file limit. Holds no exec bits and is
  often mounted `noexec` — fine: nothing is executed from the stick, it is
  copied to RAM and verified first. Start it with `bash run.sh`.
- **ext4**: also fine; Linux-only.
- **Not FAT32**: a file over 4 GiB cannot be stored on it, which rules out
  most useful GGUFs.
  Only ext4 was mounted here (as a `noexec` loop image). This kernel has no
  vfat or exfat driver, so the FAT advice comes from documentation and was not
  measured. [UNTESTED]

## On the laptop, step by step

1. On the build machine: `make`, then
   `tools/agent/usb/build_bundle.sh --out /path/to/stick/exsecutor-usb`.
   Write down the `MANIFEST sha256` it prints and keep it somewhere other than
   the stick.
2. llama.cpp: build `llama-server` with the HIP (ROCm) or Vulkan backend
   (llama.cpp's own `docs/build.md` names the current CMake options), and copy
   the binary and its shared libraries into `llama/`. ROCm needs the host's ROCm
   runtime and access to `/dev/kfd` and `/dev/dri`. The namespaces do not hide
   those devices. Vulkan needs the host's Vulkan driver. This whole step
   is [UNTESTED]: there was no GPU or llama.cpp where this was built.
3. A GGUF of the tuned model in `models/`: merge the LoRA into the base and
   convert with llama.cpp's `convert_hf_to_gguf.py`, then quantize so that the
   weights plus the KV cache fit in 8 GB of VRAM. You can also convert the
   adapter alone and pass it with `--server-arg --lora --server-arg FILE`. [UNTESTED]
4. Check those files, then pin them: `bash run.sh --pin-local` (this writes
   `LOCAL.MANIFEST`). The server is never started from unpinned files.
5. Run:
   `bash run.sh --expect-manifest SHA -- --task "exit with status 3" --expect-exit 3`
   You can also pass `--model-in-ram` (copies the model to RAM, and refuses
   unless free RAM covers the model plus `--headroom-mib`), `--save-transcripts`
   and `--dry-run` (prints the exact server and loop commands).
   `bash run.sh --help` lists the rest. Without `--model-in-ram`, the server
   mmaps the GGUF from the stick, so loading takes as long as the stick needs
   to read it. Every session also re-hashes the pinned files, which costs one
   full read of the model.

## Threat model (honest)

**Model-written programs** (the runner, per program): new user, mount, pid, net,
ipc, uts and cgroup namespaces. The root is a fresh tmpfs holding only `/prog`
and is read-only, and the old root is detached. There is no `/proc` or `/dev`,
and no network interface. stdin is `/dev/null`, and stdout and stderr are
pipes. Every capability is dropped, and these limits apply:
`RLIMIT_AS/CPU/FSIZE/NPROC/NOFILE`, `no_new_privs`, and a 10 s wall clock that
kills the whole pid namespace. A seccomp filter allows only `read write close
fstat lseek mmap munmap exit_group openat2` (+`execve` to start). That list is
the prelude's table, and strace over the repo's test programs observed exactly
it. This matters because Exsecutor's capabilities do not confine a binary:
`archivum` opens any path the process can see (spec §4.6, by design). The
selftest's `archivum_etc` program reads the host's `/etc/passwd` when run
directly, and fails inside the runner.

**The session** (run.sh): host mounts are remounted read-only inside the
session's mount namespace, so nothing can write to the disk or the stick
unless you pass `--save-transcripts`. There is no network interface except
`lo`. The work directory is a tmpfs that dies with the namespace.

**Not protected:**
- **Kernel bugs.** This is namespace isolation on the host kernel, not a VM.
- **Disk swap.** RAM pages can reach a disk swap device. `run.sh` warns if one
  is active.
- **Reads by trusted code.** `llama-server` and `loop.py` can *read* the host
  filesystem; they cannot write it or reach a network. A malicious GGUF that
  exploits `llama-server` gets that read access, plus the GPU driver's attack
  surface.
- **A compromised laptop OS.** It defeats all of this.
- **Integrity anchoring.** `MANIFEST` lives on the same stick as the files, so
  only `--expect-manifest` with a hash kept elsewhere anchors it. Nothing on
  the stick can vouch for `run.sh` itself.
- **Faked status codes.** A program can exit 124, 125 or 132 itself. The
  runner's own diagnostics (`sandbox_run:` lines on stderr) tell them apart.
- **Host root.** `RLIMIT_NPROC` does not bind when the host user is root
  (measured). There is no cgroup, so memory is bounded per process
  (`RLIMIT_AS`).

**Fails closed:** when unprivileged user namespaces are unavailable, both the
runner and `run.sh` exit 125 and run nothing. That happens with
`kernel.unprivileged_userns_clone=0`, `user.max_user_namespaces=0`, Ubuntu
23.10+ `kernel.apparmor_restrict_unprivileged_userns=1` without a profile
([UNTESTED] on such a system), or a container seccomp profile. selftest.sh
simulates the last case by denying `unshare` (and `mount`) with seccomp from
outside.
