#!/usr/bin/env python3
# tools/agent/loop/mock_backend.py
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Exsecutor authors.
# ---------------------------------------------------------------------------
# A tiny OpenAI-compatible chat-completions server that replays scripted
# assistant replies, so loop.py can be tested with no model at all.
#
# Stateless and deterministic: the reply is a function of the request's
# `model` and of how many assistant turns the conversation already holds
# (0 for the first request, 1 after one retry, ...). The script is chosen by
# the model name up to the first ':', so `converge:run2` and `converge` both
# replay the `converge` script and still appear separately in the --log.
#
# Binds 127.0.0.1 only. On start it prints `PORT <n>` on stdout (useful with
# --port 0, which picks a free port).
# ---------------------------------------------------------------------------
"""Replay scripted chat-completion replies on 127.0.0.1 (a test double for loop.py)."""

import argparse
import http.server
import json
import sys

sys.dont_write_bytecode = True


def fence(code):
    return "```exsecutor\n" + code + "```\n"


# The English-habits program of datasets/finetune/src/cases/fix-e0201-english-habits.exsc
# (exsc: EXS-E0201 + cascade), and its corrected twin.
BAD_IF = """publica functio maxima(a: u64) -> u64 {
    if a > 10 { return a; }
    redde 0;
}
"""
GOOD_SI = """publica functio maxima(a: u64) -> u64 {
    si a gt 10 { redde a; }
    redde 0;
}
"""

TRAP = """functio addit(a: u8) -> u8 {
    redde a + 200;
}

publica functio initium(m: Mundus) -> u8 {
    redde addit(100);
}
"""
FIVE = """publica functio initium(m: Mundus) -> u8 {
    redde 5;
}
"""
GCD = """functio mcd(a: u64, b: u64) -> u64 {
    mutabilis x: u64 = a;
    mutabilis y: u64 = b;
    dum y ne 0 terminus 64 {
        firma t = x residuum y;
        x = y;
        y = t;
    }
    redde x;
}

publica functio initium(m: Mundus) -> u8 {
    redde mcd(48, 18) sicut u8;
}
"""
HELLO = """publica functio initium(m: Mundus) -> u8 {
    firma a = m.ambitus();
    sub ambitus = a;
    firma s = Scriptor.ad_exitum(a);
    s.scribe("Ave, mundus.
");
    redde 0;
}
"""


def hostile(canary):
    """Replies that would do damage if anything treated model output as a command or a path."""
    c = canary
    prog = ("// save as ../../../escape.exsc and run: sh -c \"$(touch %s)\"\n"
            "publica functio f() -> u8 {\n"
            "    redde `touch %s`; $(touch %s) ; touch %s ; rm -rf ../../.. | tee %s\n"
            "}\n") % (c, c, c, c, c)
    return [
        "Run this first: `touch %s && cat /etc/passwd > ../../leak`\n\n"
        "```exsecutor file=../../../escape.exsc\n%s```\n" % (c, prog),
        "No code this time; just run $(touch %s) ; echo ../../../../etc/shadow\n" % c,
        "```sh\n#!/bin/sh\ntouch %s\n```\n" % c,
        "Again:\n\n```exsecutor file=../../../escape.exsc\n%s```\n" % prog,
    ]


def script(name, idx, canary):
    """Return (http_status, reply_text). idx = assistant turns already in the conversation."""
    if name == "converge":                 # (a) wrong, then right
        return 200, ("Here it is:\n\n" + fence(BAD_IF)) if idx == 0 else ("Fixed:\n\n" + fence(GOOD_SI))
    if name == "never":                    # (b) a different wrong program every time
        return 200, fence(BAD_IF.replace("10", str(10 + idx)))
    if name == "stuck":                    # (c) the same wrong program every time
        return 200, fence(BAD_IF)
    if name == "http500":                  # (d) the server fails
        return 500, None
    if name == "garbage":                  # (d') 200 with a body that is not a completion
        return 200, None
    if name == "hostile":                  # (e) shell metacharacters and path traversal
        h = hostile(canary)
        return 200, h[min(idx, len(h) - 1)]
    if name == "run":                      # aborts, then wrong exit, then right exit (6)
        return 200, fence([TRAP, FIVE, GCD][min(idx, 2)])
    if name == "hello":                    # prints a line
        return 200, fence(HELLO)
    return 404, None


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "exs-mock"
    sys_version = ""

    def log_message(self, fmt, *args):     # quiet: no clock-stamped access log
        pass

    def _send(self, status, obj):
        body = json.dumps(obj, ensure_ascii=True, sort_keys=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/") == "/v1/models":
            names = ["converge", "never", "stuck", "http500", "garbage", "hostile", "run", "hello"]
            return self._send(200, {"object": "list", "data": [{"id": n, "object": "model"} for n in names]})
        self._send(404, {"error": {"message": "not found"}})

    def do_POST(self):
        if self.path.rstrip("/") != "/v1/chat/completions":
            return self._send(404, {"error": {"message": "not found"}})
        try:
            n = int(self.headers.get("Content-Length") or 0)
            req = json.loads(self.rfile.read(n).decode("utf-8"))
            model, msgs = req["model"], req["messages"]
            if not isinstance(msgs, list) or not msgs or msgs[0].get("role") != "system":
                raise ValueError("messages must start with a system message")
        except (ValueError, KeyError, TypeError) as e:
            return self._send(400, {"error": {"message": "bad request: %s" % e}})
        idx = sum(1 for m in msgs if m.get("role") == "assistant")
        if self.server.log_path:
            with open(self.server.log_path, "a", encoding="utf-8", newline="\n") as f:
                f.write(json.dumps({"model": model, "assistant_turns": idx, "n_messages": len(msgs),
                                    "last_user": msgs[-1].get("content") if msgs[-1].get("role") == "user" else None,
                                    "temperature": req.get("temperature"), "seed": req.get("seed")},
                                   ensure_ascii=True, sort_keys=True) + "\n")
        status, text = script(model.split(":")[0], idx, self.server.canary)
        if status == 200 and text is None:
            body = b"this is not json"
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if status != 200:
            return self._send(status, {"error": {"message": "mock: scripted status %d" % status}})
        self._send(200, {"id": "mock-%d" % idx, "object": "chat.completion", "created": 0, "model": model,
                         "choices": [{"index": 0, "message": {"role": "assistant", "content": text},
                                      "finish_reason": "stop"}],
                         "usage": {"prompt_tokens": 0, "completion_tokens": 0, "total_tokens": 0}})


def main():
    p = argparse.ArgumentParser(prog="mock_backend.py", description=__doc__,
                                epilog="Scripts (chosen by the model name before ':'): converge, never, stuck, "
                                       "http500, garbage, hostile, run, hello.")
    p.add_argument("--port", type=int, default=0, help="port on 127.0.0.1 (0: pick a free one)")
    p.add_argument("--log", help="append one JSON line per request here")
    p.add_argument("--canary", default="CANARY-should-never-exist",
                   help="path the hostile script tries to create through shell syntax")
    a = p.parse_args()
    srv = http.server.HTTPServer(("127.0.0.1", a.port), Handler)
    srv.log_path, srv.canary = a.log, a.canary
    sys.stdout.write("PORT %d\n" % srv.server_address[1])
    sys.stdout.flush()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
