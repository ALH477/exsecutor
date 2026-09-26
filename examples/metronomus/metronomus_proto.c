// SPDX-License-Identifier: MIT
//
// metronomus_proto.c -- compiled, never linked. It includes metronomus.h and
// then a generated unit, so every prototype in the header is checked against
// the definition exsc emitted: a mismatch is a conflicting-types error. The
// generated unit's path is supplied with -DMETRONOMUS_GEN='"path"'.
#include "metronomus.h"
#include METRONOMUS_GEN
