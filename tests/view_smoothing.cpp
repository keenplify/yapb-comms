#include <navigation_aim.h>
#include <view_timing.h>
#include <movement_view.h>
#include <cassert>
#include <cmath>
#include <initializer_list>

int main () {
   struct Angles { float pitch, yaw, roll; };
   Angles view { -5.0f, 179.0f, 0.0f };
   int movementCalls = 0;
   float forwardDistance = 0.0f;
   const auto engineMovement = [&] {
      ++movementCalls;
      forwardDistance += 250.0f * 0.033f;
      view = { 0.0f, -90.0f, 0.0f }; // engine copies the route command heading
   };
   runMovementPreservingView (true, view, engineMovement);
   assert (view.pitch == -5.0f && view.yaw == 179.0f && view.roll == 0.0f);
   assert (movementCalls == 1 && forwardDistance == 250.0f * 0.033f);
   // Non-navigation movement keeps the engine's combat/precise-action result.
   runMovementPreservingView (false, view, engineMovement);
   assert (view.pitch == 0.0f && view.yaw == -90.0f && movementCalls == 2);

   constexpr float minimum = 0.000001f;
   BotViewTiming clock { 1.0f, 1.0f };
   for (int frame = 1; frame <= 10; ++frame) {
      const float now = 1.0f + frame * 0.01f;
      const float delta = clock.advance (now, true, frame % 3 == 0, 0.04f, minimum);
      assert (std::fabs (delta - 0.01f) < 0.00001f);
   }
   // Acquiring an enemy after inter-think navigation ticks must still use
   // the full combat think interval; no added camera lag in the aim controller.
   assert (std::fabs (clock.advance (1.12f, false, true, 0.04f, minimum) - 0.03f) < 0.00001f);
   assert (std::fabs (clock.advance (1.15f, false, true, 0.04f, minimum) - 0.03f) < 0.00001f);
   assert (std::fabs (clock.advance (1.16f, true, false, 0.04f, minimum) - 0.01f) < 0.00001f);
   assert (clock.advance (2.0f, true, true, 0.04f, minimum) == 0.04f);
   assert (clock.advance (0.0f, true, true, 0.04f, minimum) == minimum);

   // Cross the signed heading boundary via the short arc, at both normal and
   // slow server frame rates, without oscillating or passing the aim point.
   for (const float frameDelta : { 0.005f, 0.01f, 0.033f, 0.1f }) {
      float heading = 179.0f, velocity = 0.0f;
      for (int frame = 0; frame < 1000; ++frame) {
         const float step = navigationTurnStep (-179.0f - heading, velocity, frameDelta);
         assert (step >= 0.0f && step <= 240.0f * frameDelta + 0.0001f);
         heading += step;
         if (heading > 180.0f) heading -= 360.0f;
      }
      assert (std::fabs (heading + 179.0f) < 0.001f);
      assert (std::fabs (velocity) < 0.01f);
      const float reverse = navigationTurnStep (-10.0f, velocity, frameDelta);
      assert (reverse < 0.0f);
   }
}
