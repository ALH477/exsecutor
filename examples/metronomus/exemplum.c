// SPDX-License-Identifier: MIT
//
// exemplum.c -- metronomus driven the way a Kiln game would drive it, in C,
// against the generated unit (metronomus.h says how to make one). Two peers
// in one process, each with its own fixed-step clock, exchange DCF-Game INPUT
// bodies over a simulated link with a fixed delay, predict what they have
// not received, and roll back when a prediction was wrong. At the end both
// peers must hold the same simulation checksum for every confirmed tick.
//
// DETERMINISTIC ON PURPOSE. The "clock" is a counter advanced by a fixed,
// uneven frame time in Kiln's own TICKS unit (46,875,000 Hz: the N64's COP0
// count, and what the host shim scales CLOCK_MONOTONIC to), and the "players"
// press buttons from a pure function of the tick. A real game passes
// TICKS_DISTANCE(last, TICKS_READ()) as dt instead; nothing else changes. So
// the transcript this prints is the same on every machine and is checked
// byte for byte (exemplum.expected).
//
// Build and check: examples/metronomus/proba_c.sh does it, for gcc and clang,
// and says what it ran.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metronomus.h"

_Noreturn void exsrt_abortus(unsigned kind)
{
    fprintf(stderr, "exemplum: exsrt_abortus(%u)\n", kind);
    abort();
}

#define TICKS_PER_SECOND 46875000u  // libdragon's, on the console and the host shim
#define RATE             60u        // simulation Hz
#define PEERS            2u
#define DELAY_TICKS      3u         // one-way link delay, in simulation ticks
#define RUN_TICKS        240u       // four seconds

// A frame time that is NOT a whole number of simulation ticks: 1/57 s, with
// a 90 ms stall every 50 frames, the way a real frame loop jitters.
static uint64_t frame_dt(unsigned frame)
{
    uint64_t dt = TICKS_PER_SECOND / 57u;
    if (frame % 50u == 49u)
        dt += TICKS_PER_SECOND / 11u;
    return dt;
}

// What player p holds at tick t: changes every 7 or 11 ticks, so there is
// something to mispredict.
static uint16_t buttons_of(unsigned p, uint64_t t)
{
    uint64_t k = p == 0 ? t / 7u : t / 11u;
    return (uint16_t)(exs_alea_valor(exs_alea_pulsus(0xC0FFEEu + p, k)) & 0xFFFFu);
}

// One peer's simulation: a toy state per player, stepped with the inputs.
typedef struct {
    int64_t x[PEERS];
    uint64_t hash;
} Sim;

static void sim_step(Sim *s, uint64_t tick, const uint16_t in[PEERS])
{
    uint64_t h = exs_summa_initium();
    h = exs_summa_misce(h, tick);
    for (unsigned p = 0; p < PEERS; p++) {
        s->x[p] += (in[p] & 1u) ? 3 : -1;
        s->x[p] += (int64_t)(exs_alea_valor(exs_alea_pulsus(7u, tick)) % 3u);
        h = exs_summa_misce(h, (uint64_t)s->x[p]);
    }
    s->hash = h;
}

// The link: INPUT bodies in flight, delivered DELAY_TICKS after sending.
typedef struct {
    unsigned char body[MANDATUM_BYTES];
    unsigned from, to;
    uint64_t due;
} Packet;

static Packet wire[4096];
static unsigned wire_n;

typedef struct {
    METRONOMUS_RECORD(clock, METRONOMUS_BYTES);
    METRONOMUS_RECORD(inputs, CONSESSUS_BYTES);
    Sim history[RUN_TICKS + 1];   // state BEFORE tick t, for rollback
    uint64_t hash_at[RUN_TICKS];  // checksum after simulating tick t
    uint64_t next;                // next tick to simulate
    unsigned rollbacks, resimulated;
} Peer;

static Peer peer[PEERS];

// A `@transitus` field is big-endian by declaration, on every host.
static uint64_t be64(const unsigned char *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++)
        v = v << 8 | p[i];
    return v;
}

// A native-order record field (Consessus is host memory, not wire).
static uint64_t ne64(const unsigned char *p)
{
    uint64_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

static void simulate_from(Peer *me, uint64_t from, uint64_t to)
{
    for (uint64_t t = from; t < to && t < RUN_TICKS; t++) {
        uint16_t in[PEERS];
        for (unsigned p = 0; p < PEERS; p++) {
            uint64_t v = exs_lege(me->inputs, p, t);
            if (v == METRONOMUS_NULLUS) {
                // The 64-tick rule was broken: a rollback was collected too
                // late and its inputs are gone. Never mask this to buttons.
                fprintf(stderr, "exemplum: no input for player %u at tick %llu\n",
                        p, (unsigned long long)t);
                exit(4);
            }
            in[p] = (uint16_t)(v & 0xFFFFu);
        }
        me->history[t + 1] = me->history[t];
        sim_step(&me->history[t + 1], t, in);
        me->hash_at[t] = me->history[t + 1].hash;
    }
}

int main(void)
{
    for (unsigned i = 0; i < PEERS; i++) {
        exs_metronomum_para(peer[i].clock, TICKS_PER_SECOND, RATE, 8);
        if (!exs_metronomus_valet(peer[i].clock) ||
            exs_consessum_para(peer[i].inputs, PEERS, 0) != 0)
            return 2;
        memset(&peer[i].history[0], 0, sizeof peer[i].history[0]);
    }

    uint64_t now = 0;  // shared "real" time in simulation ticks, for the link
    for (unsigned frame = 0; now < RUN_TICKS + DELAY_TICKS + 1; frame++) {
        for (unsigned i = 0; i < PEERS; i++) {
            Peer *me = &peer[i];
            uint64_t n = exs_pulsa(me->clock, frame_dt(frame));
            for (uint64_t k = 0; k < n; k++) {
                uint64_t t = me->next;
                if (t < RUN_TICKS) {
                    // Local input: recorded at once, and sent.
                    exs_inscribe(me->inputs, i, t, buttons_of(i, t));
                    for (unsigned j = 0; j < PEERS; j++) {
                        if (j == i)
                            continue;
                        Packet *pk = &wire[wire_n++];
                        exs_mandatum_scribe(pk->body, t, buttons_of(i, t));
                        pk->from = i;
                        pk->to = j;
                        pk->due = t + DELAY_TICKS;
                    }
                }
                simulate_from(me, t, t + 1);
                me->next = t + 1;
            }
        }
        now = peer[0].next < peer[1].next ? peer[0].next : peer[1].next;

        // Deliver what has arrived; a contradicted prediction rolls back.
        for (unsigned w = 0; w < wire_n; w++) {
            Packet *pk = &wire[w];
            if (pk->due > now || pk->due == UINT64_MAX)
                continue;
            unsigned char m[MANDATUM_BYTES];
            exs_mandatum_lege(m, pk->body);
            uint64_t t = (uint64_t)m[0] << 24 | (uint64_t)m[1] << 16 |
                         (uint64_t)m[2] << 8 | m[3];
            uint64_t b = (uint64_t)m[4] << 8 | m[5];
            exs_inscribe(peer[pk->to].inputs, pk->from, t, b);
            pk->due = UINT64_MAX;
        }
        for (unsigned i = 0; i < PEERS; i++) {
            Peer *me = &peer[i];
            uint64_t r = exs_revertendum_cape(me->inputs);
            if (r != METRONOMUS_NULLUS && r < me->next) {
                me->rollbacks++;
                me->resimulated += (unsigned)(me->next - r);
                simulate_from(me, r, me->next);
            }
        }
    }

    // Every tick both peers simulated must now agree.
    unsigned agree = 0;
    for (unsigned t = 0; t < RUN_TICKS; t++)
        agree += peer[0].hash_at[t] == peer[1].hash_at[t];

    for (unsigned i = 0; i < PEERS; i++)
        printf("peer %u: ticks %llu, slipped %llu, confirmed below %llu, "
               "rollbacks %u, resimulated %u, final hash %016llx\n",
               i, (unsigned long long)be64(peer[i].clock + METRONOMUS_PULSUS),
               (unsigned long long)be64(peer[i].clock + METRONOMUS_LAPSI),
               (unsigned long long)ne64(peer[i].inputs + CONSESSUS_CONFIRMATUM),
               peer[i].rollbacks, peer[i].resimulated,
               (unsigned long long)peer[i].hash_at[RUN_TICKS - 1]);
    printf("ticks agreeing: %u of %u\n", agree, RUN_TICKS);

    // The wall clock, for a HUD: a fixed instant, UTC and UTC+05:30.
    unsigned char h[HORA_BYTES];
    exs_hora_civilis(h, 1758844800123ull, 0);
    printf("%04u-%02u-%02u %02u:%02u:%02u.%03u UTC\n",
           h[0] << 8 | h[1], h[2], h[3], h[4], h[5], h[6], h[8] << 8 | h[9]);
    exs_hora_civilis(h, 1758844800123ull, 330);
    printf("%04u-%02u-%02u %02u:%02u:%02u.%03u +05:30\n",
           h[0] << 8 | h[1], h[2], h[3], h[4], h[5], h[6], h[8] << 8 | h[9]);
    return agree == RUN_TICKS ? 0 : 1;
}
