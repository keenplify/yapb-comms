// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <initializer_list>

// Bounded, deterministic dry-ground movement model. World queries stay on the
// engine thread. This is route prediction, never evidence of a real traversal.
struct VirtualVec {
   float x {}, y {}, z {};
   VirtualVec operator + (VirtualVec b) const { return { x + b.x, y + b.y, z + b.z }; }
   VirtualVec operator - (VirtualVec b) const { return { x - b.x, y - b.y, z - b.z }; }
   VirtualVec operator * (float n) const { return { x * n, y * n, z * n }; }
   float dot (VirtualVec b) const { return x * b.x + y * b.y + z * b.z; }
   float horizontal () const { return std::sqrt (x * x + y * y); }
};

struct VirtualTrace {
   VirtualVec end {}, normal {};
   float fraction { 1.0f };
   bool solid {}, unsupported {};
   bool centerSupported { true };
};

struct VirtualPhysics {
   float gravity { 800 }, maxspeed { 250 }, accelerate { 5 }, airaccelerate { 10 };
   float friction { 4 }, stepsize { 18 }, stopspeed { 75 };
};

inline float virtualSupportDepth (VirtualVec normal, float stepHeight) {
   const float slopeDepth = normal.z >= 0.7f
      ? 3 + 16 * std::sqrt (std::fmax (0.0f, 1 - normal.z * normal.z)) / normal.z : 0;
   return std::fmax (stepHeight + 2, slopeDepth);
}

template <typename Trace>
void virtualPreferStanding (VirtualVec &position, bool &crouch, Trace trace) {
   if (!crouch) return;
   const auto standing = position + VirtualVec { 0, 0, 18 };
   const auto fit = trace (standing, standing, false);
   if (!fit.solid && !fit.unsupported && fit.fraction == 1) {
      position = standing;
      crouch = false;
   }
}

template <typename Trace>
bool virtualFloorCandidate (VirtualVec baseline, VirtualVec &position, bool &crouch, Trace trace) {
   // An overhead-only floor probe can start inside a low ceiling and discard
   // an otherwise valid crouch tunnel. Retry at body and crouching height.
   for (float offset : { 48.0f, 0.0f, -18.0f }) {
      auto start = baseline + VirtualVec { 0, 0, offset };
      const auto floor = trace (start, start - VirtualVec { 0, 0, 192 }, true);
      if (floor.solid || floor.unsupported || !floor.centerSupported || floor.fraction >= 1 || floor.normal.z < 0.7f) continue;
      position = floor.end + VirtualVec { 0, 0, 19 };
      auto fit = trace (position, position, false);
      crouch = fit.solid || fit.unsupported || fit.fraction < 1;
      if (crouch) {
         position.z -= 18;
         fit = trace (position, position, true);
      }
      if (!fit.solid && !fit.unsupported && fit.fraction == 1) return true;
   }
   return false;
}

enum class VirtualResult { Running, Arrived, Rejected };

inline bool virtualRouteCorridor (VirtualVec source, VirtualVec target, VirtualVec point) {
   const auto direction = target - source;
   const auto offset = point - source;
   const float length = direction.horizontal ();
   if (length < 1) return offset.horizontal () <= 2;
   // Sliding around a corner needs another waypoint at that corner.
   return std::fabs (offset.x * direction.y - offset.y * direction.x) <= 2 * length;
}

// A direct walking corridor, sampled with the whole player hull. Each sample
// may step up/down, but cannot cross an unsupported gap or detour around a wall.
template <typename Trace>
VirtualVec virtualWalkFrontier (VirtualVec from, VirtualVec to, bool crouch, float stepHeight, Trace trace) {
   const auto delta = to - from;
   const float distance = delta.horizontal ();
   if (distance < 1) return from;
   const int samples = static_cast <int> (std::ceil (distance / 8));
   auto position = from;
   const auto startFit = trace (position, position, crouch);
   if (startFit.solid || startFit.unsupported || startFit.fraction < 1) return from;
   for (int i = 1; i <= samples; ++i) {
      const auto desired = from + delta * (static_cast <float> (i) / samples);
      auto across = trace (position, VirtualVec { desired.x, desired.y, position.z }, crouch);
      if (!across.solid && !across.unsupported && across.fraction < 1) {
         const auto up = trace (position, position + VirtualVec { 0, 0, stepHeight }, crouch);
         if (up.solid || up.unsupported) break;
         across = trace (up.end, VirtualVec { desired.x, desired.y, up.end.z }, crouch);
      }
      if (across.solid || across.unsupported || across.fraction < 1) break;
      auto down = trace (across.end, across.end - VirtualVec { 0, 0, stepHeight * 2 + 2 }, crouch);
      if (down.solid || down.unsupported || down.fraction >= 1 || down.normal.z < 0.7f
         || down.end.z < position.z - stepHeight - 0.1f) break;
      position = down.end;
   }
   return position;
}

template <typename Trace>
bool virtualWalkCorridor (VirtualVec from, VirtualVec to, bool crouch, float stepHeight, Trace trace) {
   const auto end = virtualWalkFrontier (from, to, crouch, stepHeight, trace);
   return (end - to).horizontal () <= 0.1f && std::fabs (end.z - to.z) <= 2;
}

struct VirtualPlayer {
   VirtualVec origin {}, velocity {}, source {}, target {};
   float elapsed {}, landing {};
   bool crouched {}, targetCrouch {}, grounded {}, jumped {}, airborne {}, failed {};
   int mode {};
   bool constrainRoute {};

   float feet () const { return origin.z - (crouched ? 18.0f : 36.0f); }

   template <typename Trace> bool slide (VirtualVec &pos, VirtualVec &vel, float dt, Trace &trace) {
      VirtualVec planes[4] {};
      int count = 0;
      float remaining = dt;
      for (int bump = 0; bump < 4; ++bump) {
         auto hit = trace (pos, pos + vel * remaining, crouched);
         if (hit.solid || hit.unsupported) return false;
         pos = hit.end;
         if (hit.fraction >= 1.0f) return true;
         remaining *= 1.0f - hit.fraction;
         planes[count++] = hit.normal;
         for (int i = 0; i < count; ++i) {
            const float into = vel.dot (planes[i]);
            if (into < 0) vel = vel - planes[i] * into;
         }
         // Stop at conflicting planes rather than squeeze through a corner.
         for (int i = 0; i < count; ++i) if (vel.dot (planes[i]) < -0.01f) { vel = {}; return true; }
         if (vel.dot (vel) < 0.0001f) return true;
      }
      vel = {};
      return true;
   }

   template <typename Trace> VirtualResult step (const VirtualPhysics &p, float dt, Trace &trace) {
      if (failed || dt <= 0 || dt > 0.02f) return VirtualResult::Rejected;
      elapsed += dt;
      if (elapsed > 3.0f || origin.z < target.z - 192.0f) return VirtualResult::Rejected;
      const bool wantCrouch = (mode & 1) != 0 || (mode == 4 && jumped);
      if (wantCrouch != crouched) {
         // GoldSrc shifts center down on grounded duck, up on airborne duck.
         auto next = origin;
         next.z += (wantCrouch ? 18.0f : -18.0f) * (grounded ? -1.0f : 1.0f);
         auto fit = trace (next, next, wantCrouch);
         if (!fit.solid && !fit.unsupported && fit.fraction == 1.0f) { origin = next; crouched = wantCrouch; }
         else if (wantCrouch) return VirtualResult::Rejected;
      }
      auto floor = trace (origin, origin - VirtualVec { 0, 0, 2 }, crouched);
      if (floor.solid || floor.unsupported) return VirtualResult::Rejected;
      grounded = velocity.z <= 0 && floor.fraction < 1 && floor.normal.z >= 0.7f;
      if (grounded) { origin = floor.end; velocity.z = 0; }
      const bool inside = (target - origin).horizontal () <= 12.0f
         && std::fabs (feet () - (target.z - (targetCrouch ? 18.0f : 36.0f))) < 8.0f;
      if (inside && grounded && floor.centerSupported) {
         landing += dt;
         if (landing >= 0.25f && (mode < 2 || (jumped && airborne))) return VirtualResult::Arrived;
      }
      else landing = 0;
      if (grounded) {
         const float speed = velocity.horizontal ();
         if (speed > 0) {
            const float control = speed < p.stopspeed ? p.stopspeed : speed;
            const float next = speed - control * p.friction * dt;
            const float scale = next > 0 ? next / speed : 0;
            velocity.x *= scale; velocity.y *= scale;
         }
      }
      auto wish = target - origin;
      wish.z = 0;
      const float distance = wish.horizontal ();
      if (distance > 0) wish = wish * (1.0f / distance);
      // Standard CS knife running speed, capped by sv_maxspeed. Crouch is 1/3.
      const float wishspeed = inside ? 0 : p.maxspeed * (crouched ? 0.33333333f : 1.0f);
      const float cap = !grounded && wishspeed > 30 ? 30 : wishspeed;
      const float add = cap - velocity.dot (wish);
      float accel = (grounded ? p.accelerate : p.airaccelerate) * wishspeed * dt;
      if (accel > add) accel = add;
      if (accel > 0) velocity = velocity + wish * accel;
      if (mode >= 2 && !jumped && grounded && (origin - source).horizontal () >= 12) {
         velocity.z = 268.328157f; // standard GoldSrc jump impulse
         jumped = true; grounded = false;
      }
      const bool wasGrounded = grounded;
      if (!grounded) velocity.z -= p.gravity * dt * 0.5f;
      const auto before = origin;
      const auto initialVelocity = velocity;
      if (!slide (origin, velocity, dt, trace)) return VirtualResult::Rejected;
      // Try a full hull step; never teleport over a thin wall or across a gap.
      if (wasGrounded && (origin - before).horizontal () + 0.01f < initialVelocity.horizontal () * dt) {
         const auto up = trace (before, before + VirtualVec { 0, 0, p.stepsize }, crouched);
         if (!up.solid && !up.unsupported && up.fraction == 1) {
            auto raised = up.end;
            auto raisedVelocity = initialVelocity;
            raisedVelocity.z = 0;
            if (slide (raised, raisedVelocity, dt, trace)) {
               const auto down = trace (raised, raised - VirtualVec { 0, 0, p.stepsize + 2 }, crouched);
               if (!down.solid && !down.unsupported && down.fraction < 1 && down.normal.z >= 0.7f
                  && (down.end - before).horizontal () > (origin - before).horizontal ()) {
                  origin = down.end; velocity = raisedVelocity;
               }
            }
         }
      }
      if (!wasGrounded) velocity.z -= p.gravity * dt * 0.5f;
      if (constrainRoute && !virtualRouteCorridor (source, target, origin)) return VirtualResult::Rejected;
      if (jumped && !grounded && origin.z > before.z + 0.01f) airborne = true;
      return VirtualResult::Running;
   }
};

// Ladder contact comes from the ladder brush's actual expanded collision hull,
// not just its bounding box. Approach/exit use the ordinary movement model.
struct VirtualLadderPlayer {
   VirtualPlayer player {};
   VirtualVec mount {}, destination {};
   VirtualVec rungs[128] {};
   int rungCount {}, phase {};
   float low {}, high {}, elapsed {}, lastRungHeight {};
   bool up {};

   template <typename Trace, typename Contact>
   VirtualResult step (const VirtualPhysics &physics, float dt, Trace &trace, Contact &contact) {
      elapsed += dt;
      if (dt <= 0 || dt > 0.02f || elapsed > (high - low) / 100.0f + 10.0f) return VirtualResult::Rejected;
      if (phase == 0) {
         player.target = mount;
         if ((player.origin - mount).horizontal () <= 16 && contact (player.origin) && (up || !player.grounded)) {
            phase = 1;
            player.velocity = {};
            rungs[rungCount++] = player.origin;
            lastRungHeight = player.origin.z;
         }
         else {
            const auto result = player.step (physics, dt, trace);
            if (result == VirtualResult::Rejected) return result;
            return VirtualResult::Running;
         }
      }
      if (phase == 1) {
         const bool attached = contact (player.origin);
         if (up && !attached) {
            if (player.origin.z < high + 24) return VirtualResult::Rejected;
            phase = 2;
            player.velocity = { 0, 0, 200 };
            player.grounded = false;
            player.elapsed = player.landing = 0;
         }
         else if (!up && player.origin.z <= low + 2) {
            phase = 2;
            player.velocity = {};
            player.grounded = true;
            player.elapsed = player.landing = 0;
         }
         else {
            if (!attached || player.origin.z > high + 40 || player.origin.z < low - 4) return VirtualResult::Rejected;
            const auto hit = trace (player.origin, player.origin + VirtualVec { 0, 0, (up ? 200.0f : -200.0f) * dt }, false);
            if (hit.solid || hit.unsupported || hit.fraction < 1) return VirtualResult::Rejected;
            player.origin = hit.end;
            if (std::fabs (player.origin.z - lastRungHeight) >= 96) {
               if (rungCount >= 127) return VirtualResult::Rejected;
               rungs[rungCount++] = player.origin;
               lastRungHeight = player.origin.z;
            }
            return VirtualResult::Running;
         }
         // Last contact point, never a grounded landing requirement mid-climb.
         auto last = player.origin;
         if (up) last.z -= 4;
         if (!contact (last) || rungCount >= 128) return VirtualResult::Rejected;
         rungs[rungCount++] = last;
      }
      player.target = destination;
      return player.step (physics, dt, trace);
   }
};
