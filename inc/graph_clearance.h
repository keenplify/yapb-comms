#pragma once

// GoldSrc can report a full fraction even when a trace starts in solid.
// Never use an epsilon here: a hit just before the destination is still a wall.
template <typename Trace>
bool graphTraceIsClear (const Trace &trace) {
   return !trace.fStartSolid && !trace.fAllSolid && trace.flFraction == 1.0f;
}
