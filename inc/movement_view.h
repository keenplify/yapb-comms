// SPDX-License-Identifier: MIT
#pragma once

template <typename Angles, typename Move>
inline void runMovementPreservingView (bool navigationView, Angles &view, Move move) {
   const Angles lookAngles = view;
   move ();
   // GoldSrc overwrites the view with the command's movement heading. This
   // only repairs navigation presentation after that command has completed.
   if (navigationView) view = lookAngles;
}
