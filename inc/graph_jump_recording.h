// SPDX-License-Identifier: MIT
#pragma once

enum class GraphJumpResult { Pending, Landed, Rejected };

struct GraphJumpRecording {
   float started {}, groundedSince { -1 };
   bool airborne {};

   GraphJumpResult observe (float now, bool alive, bool grounded, bool walking) {
      if (!alive || !walking || now - started > 10) return GraphJumpResult::Rejected;
      if (!grounded) { airborne = true; groundedSince = -1; return GraphJumpResult::Pending; }
      if (!airborne) return now - started > 0.4f ? GraphJumpResult::Rejected : GraphJumpResult::Pending;
      if (groundedSince < 0) groundedSince = now;
      return now - groundedSince >= 0.12f ? GraphJumpResult::Landed : GraphJumpResult::Pending;
   }
};
