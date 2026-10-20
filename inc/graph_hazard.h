#pragma once

// A trigger_hurt is non-solid, so normal graph traces can pass through it.
// Expand its bounds by the standing player hull before testing a node or link.
struct GraphHazardBounds {
   float minX, minY, minZ;
   float maxX, maxY, maxZ;
};

template <typename Point>
bool graphSegmentTouchesHazard (const GraphHazardBounds &hazard, const Point &from, const Point &to) {
   const float start[] = { from.x, from.y, from.z };
   const float end[] = { to.x, to.y, to.z };
   const float mins[] = { hazard.minX - 16.0f, hazard.minY - 16.0f, hazard.minZ - 36.0f };
   const float maxs[] = { hazard.maxX + 16.0f, hazard.maxY + 16.0f, hazard.maxZ + 36.0f };
   float enter = 0.0f;
   float leave = 1.0f;

   for (int axis = 0; axis < 3; ++axis) {
      const float delta = end[axis] - start[axis];
      if (delta == 0.0f) {
         if (start[axis] < mins[axis] || start[axis] > maxs[axis]) {
            return false;
         }
         continue;
      }

      float near = (mins[axis] - start[axis]) / delta;
      float far = (maxs[axis] - start[axis]) / delta;
      if (near > far) {
         const float swap = near;
         near = far;
         far = swap;
      }
      if (near > enter) {
         enter = near;
      }
      if (far < leave) {
         leave = far;
      }
      if (enter > leave) {
         return false;
      }
   }
   return true;
}
