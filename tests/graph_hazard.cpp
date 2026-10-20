#include <graph_hazard.h>
#include <cassert>

struct Point { float x, y, z; };

int main () {
   const GraphHazardBounds hurt { 100.0f, 100.0f, -446.0f, 200.0f, 200.0f, -316.0f };

   // A node inside the damage brush, and a route crossing it, are unsafe.
   assert (graphSegmentTouchesHazard (hurt, Point { 150.0f, 150.0f, -411.0f }, Point { 150.0f, 150.0f, -411.0f }));
   assert (graphSegmentTouchesHazard (hurt, Point { 0.0f, 150.0f, -411.0f }, Point { 300.0f, 150.0f, -411.0f }));

   // The standing player hull can touch a brush even when its origin does not.
   assert (graphSegmentTouchesHazard (hurt, Point { 84.0f, 150.0f, -411.0f }, Point { 84.0f, 150.0f, -411.0f }));
   assert (!graphSegmentTouchesHazard (hurt, Point { 83.0f, 150.0f, -411.0f }, Point { 83.0f, 150.0f, -411.0f }));

   // A bridge well above the volume and a route alongside it remain usable.
   assert (!graphSegmentTouchesHazard (hurt, Point { 0.0f, 150.0f, -200.0f }, Point { 300.0f, 150.0f, -200.0f }));
   assert (!graphSegmentTouchesHazard (hurt, Point { 0.0f, 50.0f, -411.0f }, Point { 300.0f, 50.0f, -411.0f }));
}
