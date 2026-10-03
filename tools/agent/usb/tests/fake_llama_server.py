#!/usr/bin/env python3
# tools/agent/usb/tests/fake_llama_server.py
# SPDX-License-Identifier: GPL-3.0-or-later
"""fake_llama_server.py -m MODEL --host H --port P [ignored llama-server args]

Selftest STUB standing in for llama.cpp's llama-server, so that run.sh's whole
session (loopback-only network namespace, server start, /health wait, loop,
runner, transcript, wipe) can be exercised without a model. It is NOT
llama.cpp and proves nothing about llama.cpp, a GPU, or a model.

GET /health -> 200. POST /v1/chat/completions -> a scripted reply chosen by
how many assistant turns the request already holds: first a program written
with English habits (`return`, which exsc rejects), then the corrected one,
which prints `salve` and exits 3."""
import http.server
import json
import sys

BAD = """publica functio initium(m: Mundus) -> u8 {
    firma a = m.ambitus();
    sub ambitus = a;
    firma s = Scriptor.ad_exitum(a);
    s.scribe("salve");
    return 3;
}
"""
GOOD = BAD.replace("return 3;", "redde 3;")
REPLIES = [BAD, GOOD]


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.send(200 if self.path == "/health" else 404, {"status": "ok"})

    def do_POST(self):
        req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
        n = sum(1 for m in req.get("messages", []) if m.get("role") == "assistant")
        code = REPLIES[min(n, len(REPLIES) - 1)]
        text = "```exsecutor\n" + code + "```\n"
        self.send(200, {"choices": [{"index": 0, "finish_reason": "stop",
                                     "message": {"role": "assistant", "content": text}}],
                        "model": req.get("model", "stub")})


def main(argv):
    if "-h" in argv or "--help" in argv:
        sys.stdout.write(__doc__ + "\n")
        return 0
    host, port = "127.0.0.1", 8080
    for i, a in enumerate(argv):
        if a == "--host":
            host = argv[i + 1]
        elif a == "--port":
            port = int(argv[i + 1])
    http.server.HTTPServer((host, port), H).serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
