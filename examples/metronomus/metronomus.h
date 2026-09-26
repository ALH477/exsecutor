// SPDX-License-Identifier: MIT
//
// metronomus.h -- the C face of metronomus.exsc, for Kiln (and any other C
// host), after `exsc aedifica --hospes ROW --emitte c metronomus.exsc -o
// metronomus_ROW.gen.c`. ROW is mips64-none-o64 for the N64 and x86_64-linux
// for the PC/host build; the prototypes below are the same on both, because
// library mode (docs/design/c-backend.md D1) passes every integer as uint64_t
// and every address as unsigned char *, and no record here holds a `mensura`.
//
// HAND-WRITTEN, and checked rather than trusted: metronomus_proto.c includes
// this header and then a generated unit, so a prototype here that disagrees
// with a definition there is a conflicting-types ERROR, not silent UB. Kiln's
// exsec_proto_check.c does the same for the StreamDB reader.
//
// MIT, like the rest of what a host links: the header is a statement of the
// generated unit's ABI, and Exsecutor's exception (LICENSE.EXCEPTION) leaves
// generated code to its user.
//
// OWNERSHIP. Every record is caller-owned memory, passed by address; the
// library never allocates and never keeps a pointer. An aggregate RESULT
// arrives through a hidden first pointer argument the caller supplies.
//
// TIME. Nothing here reads a clock. The host reads its own -- libdragon's
// TICKS_READ() (46,875,000 Hz on the console, CLOCK_MONOTONIC scaled to the
// same rate on the host shim), clock_gettime in nanoseconds, a sample count --
// and passes the DIFFERENCE to metronomus_pulsa as `dt`. Same readings, same
// ticks, on every machine.

#pragma once

#include <stdint.h>

// ---- sizes and layouts ---------------------------------------------------

// `@transitus` records: packed, big-endian, the language's layout (spec 5.2),
// so these bytes are the same on the console and the PC and can be saved,
// sent over DCF or compared as they are.
#define METRONOMUS_BYTES   40u  // Metronomus: the fixed-step clock
#define HORA_BYTES         10u  // Hora: a civil date and time
#define MANDATUM_BYTES      6u  // Mandatum: DCF-Game INPUT body

// Metronomus field offsets (all big-endian).
enum {
    METRONOMUS_PULSUS    = 0,   // u64 ticks elapsed
    METRONOMUS_RELIQUUM  = 8,   // u64 accumulator
    METRONOMUS_LAPSI     = 16,  // u64 ticks dropped by the catch-up cap
    METRONOMUS_FONS      = 24,  // u32 source frequency, Hz
    METRONOMUS_CADENTIA  = 28,  // u16 tick rate, Hz
    METRONOMUS_MAXIMI    = 30,  // u16 catch-up cap, ticks per pulse
    METRONOMUS_CELERITAS = 32,  // u16 speed, Q8 (256 = real time)
};

// Hora field offsets.
enum {
    HORA_ANNUS = 0, HORA_MENSIS = 2, HORA_DIES = 3, HORA_HORA = 4,
    HORA_MINUTUM = 5, HORA_SECUNDUM = 6, HORA_HEBDOMAS = 7, HORA_MILLESIMUM = 8,
};

// Native-order records (host memory only; never put these on a wire).
// Derived from their declarations, u64 arrays first so that the natural and
// the packed layout agree; confirmed against the generated unit's offsets.
#define HOROLOGIA_BYTES    272u   // 16 x u64 meta, 16 x u64 periodus, 16 x u8 status
#define CONSONANTIA_BYTES  144u   // u64, u64, 8 x u64, 8 x i64
#define CONSESSUS_BYTES   5656u   // 3 x u64, 512 x u64, 512 x u16, 512 x u8
#define CONSESSUS_CONFIRMATUM 8u  // u64: every tick below it is final

#define METRONOMUS_NULLUS  UINT64_MAX  // "no tick"
#define METRONOMUS_PRAEDICTUM 0x10000u // exs_lege's prediction bit

// Declare records as aligned byte arrays, e.g.
//   static METRONOMUS_RECORD(clock, METRONOMUS_BYTES);
#define METRONOMUS_RECORD(name, bytes) \
    _Alignas(8) unsigned char name[bytes]

// ---- the one import ---------------------------------------------------------

// A trap: kind 1 is a numeric fault (an overflow a precondition should have
// excluded), 5 a `terminus` bound. The host supplies it; Kiln's demos assert.
_Noreturn void exsrt_abortus(unsigned kind);

// ---- 1-2. arithmetic ------------------------------------------------------------

uint64_t exs_quotus(uint64_t n, uint64_t d);               // n / d; UINT64_MAX if d == 0
uint64_t exs_residuum(uint64_t n, uint64_t d);             // n % d; n if d == 0
uint64_t exs_multiplica_modulo(uint64_t a, uint64_t b);    // a * b mod 2^64
uint64_t exs_proportio(uint64_t n, uint64_t a, uint64_t b); // floor(n * a / b), a, b < 2^32

// ---- 3. the fixed-step clock --------------------------------------------------

// Fill `m` (40 bytes). Refused -- cadentia 0, pulsa returns 0 -- unless
// 1 <= fons < 2^31, 1 <= cadentia <= 65535, 1 <= maximi <= 65535.
void exs_metronomum_para(unsigned char *m, uint64_t fons, uint64_t cadentia, uint64_t maximi);
uint64_t exs_metronomus_valet(unsigned char *m);           // 1 if accepted
uint64_t exs_celeritatem_pone(unsigned char *m, uint64_t q8); // 0 ok, 1 if q8 > 65535
// Advance by dt source units; returns the ticks to simulate now.
uint64_t exs_pulsa(unsigned char *m, uint64_t dt);
uint64_t exs_fractio(unsigned char *m);                    // render alpha, Q16
uint64_t exs_tempus_pulsus(unsigned char *m, uint64_t p);  // source time of tick p
uint64_t exs_pulsus_ex_tempore(uint64_t fons, uint64_t cadentia, uint64_t t);
uint64_t exs_pulsus_ex_millesimis(uint64_t cadentia, uint64_t ms); // rounded up

// ---- 4. the wall clock -----------------------------------------------------------

// `ms` since the Unix epoch and a zone offset in minutes east (an int64_t
// passed as its two's-complement bits) into the 10 bytes at `hora`. Clamped
// to the epoch below and to 65535-12-31T23:59:59.999 above (annus is u16);
// never traps.
void exs_hora_civilis(unsigned char *hora, uint64_t ms, uint64_t zona);
// The inverse, for setting a watch: the local time in the 10 bytes at `hora`
// (hebdomas ignored) and its zone, to Unix ms; METRONOMUS_NULLUS for a date
// or time that does not exist or falls before the epoch.
uint64_t exs_tempus_ex_hora(unsigned char *hora, uint64_t zona);
uint64_t exs_dies_mensis(uint64_t annus, uint64_t mensis); // 0 if no such month

// ---- 5. timers -----------------------------------------------------------------

// `h` is HOROLOGIA_BYTES, zeroed. Timer ids are 0..15.
uint64_t exs_arma(unsigned char *h, uint64_t i, uint64_t nunc, uint64_t mora, uint64_t periodus);
uint64_t exs_exstingue(unsigned char *h, uint64_t i);
uint64_t exs_restat(unsigned char *h, uint64_t i, uint64_t nunc);
uint64_t exs_excita(unsigned char *h, uint64_t nunc);      // mask of timers fired

// ---- 6. Punctim's wire ----------------------------------------------------------

void exs_mandatum_scribe(unsigned char *out6, uint64_t pulsus, uint64_t claves);
void exs_mandatum_lege(unsigned char *out6, unsigned char *in6);
uint64_t exs_imum(uint64_t x, uint64_t bitus);             // low bits; 0 -> 0, >= 64 -> x
uint64_t exs_tempus24(uint64_t us);                        // DeModFrame bytes 12..14
uint64_t exs_revolve(uint64_t prius, uint64_t crudum, uint64_t bitus); // unwrap; bitus 1..63 else NULLUS

// ---- 7-8. clock offset, round trip, time dilation --------------------------------

uint64_t exs_dimidium(uint64_t x_i64);                     // floor(x / 2), signed
// `s` receives { u64 reditus; i64 dislocatio; u8 valet }, native byte order,
// PACKED: 17 bytes, valet at offset 16 (the generated unit declares its own
// Specimen temporaries as `_Alignas(1) unsigned char [17]`). Pass >= 17.
void exs_specimen_proba(unsigned char *s, uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3);
// `c` is CONSONANTIA_BYTES, zeroed. 0 accepted, 1 rejected.
uint64_t exs_consonantiam_nota(unsigned char *c, uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3);
uint64_t exs_reditus_levis(unsigned char *c);              // smoothed round trip
uint64_t exs_dislocatio_optima(unsigned char *c);          // int64_t offset, as bits
uint64_t exs_pulsus_remotus(unsigned char *m, uint64_t nunc, uint64_t d_i64);
uint64_t exs_celeritatem_elige(uint64_t ante_i64);         // Q8 speed

// ---- 9. the lockstep / rollback input buffer ------------------------------------

// `c` is CONSESSUS_BYTES. 0 ok, 1 if lusores is not 1..8.
uint64_t exs_consessum_para(unsigned char *c, uint64_t lusores, uint64_t primus);
uint64_t exs_sedes(uint64_t lusor, uint64_t pulsus);
uint64_t exs_habet(unsigned char *c, uint64_t lusor, uint64_t pulsus);
uint64_t exs_paratus(unsigned char *c, uint64_t pulsus);   // 1: all inputs real
// 0 new, 1 repeat, 2 contradicted a prediction (rollback), 3 too old,
// 4 too far ahead, 5 no such player, 6 contradicts a real input.
uint64_t exs_inscribe(unsigned char *c, uint64_t lusor, uint64_t pulsus, uint64_t claves);
// Buttons in bits 0..15, METRONOMUS_PRAEDICTUM set if predicted. Or
// METRONOMUS_NULLUS, which is a FAULT and must not be masked to buttons: no
// such player, a tick >= 64 ahead of the watermark, or a tick below it whose
// slot was reused -- the 64-tick rule: collect exs_revertendum_cape and
// resimulate before the window moves 64 ticks past the tick it names.
uint64_t exs_lege(unsigned char *c, uint64_t lusor, uint64_t pulsus);
uint64_t exs_revertendum_cape(unsigned char *c);           // tick or METRONOMUS_NULLUS

// ---- 10. randomness and desync checks ------------------------------------------

uint64_t exs_alea_semen(uint64_t semen);                   // kiln_rng_seed's remap
uint64_t exs_alea_gradere(uint64_t x);                     // kiln_rng_u32's state step
uint64_t exs_alea_valor(uint64_t x);                       // ... and its output
uint64_t exs_alea_pulsus(uint64_t semen, uint64_t pulsus); // per-tick state
uint64_t exs_summa_initium(void);                          // FNV-1a 64 basis
uint64_t exs_summa_misce(uint64_t h, uint64_t w);          // fold one u64
