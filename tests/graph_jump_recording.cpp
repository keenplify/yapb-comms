#include <graph_jump_recording.h>
#include <cassert>

int main () {
   GraphJumpRecording jump {};
   assert (jump.observe (0.1f, true, true, true) == GraphJumpResult::Pending);
   assert (jump.observe (0.2f, true, false, true) == GraphJumpResult::Pending);
   assert (jump.observe (0.5f, true, true, true) == GraphJumpResult::Pending);
   assert (jump.observe (0.7f, true, true, true) == GraphJumpResult::Landed);
   GraphJumpRecording blocked {};
   assert (blocked.observe (0.5f, true, true, true) == GraphJumpResult::Rejected);
   GraphJumpRecording dead {};
   assert (dead.observe (0.2f, false, false, true) == GraphJumpResult::Rejected);
   GraphJumpRecording noclip {};
   assert (noclip.observe (0.2f, true, false, false) == GraphJumpResult::Rejected);
   GraphJumpRecording falling {};
   assert (falling.observe (10.1f, true, false, true) == GraphJumpResult::Rejected);
   GraphJumpRecording bounce {};
   bounce.observe (0.1f, true, false, true);
   bounce.observe (0.5f, true, true, true);
   bounce.observe (0.55f, true, false, true);
   assert (bounce.observe (0.6f, true, true, true) == GraphJumpResult::Pending);
   assert (bounce.observe (0.7f, true, true, true) == GraphJumpResult::Pending);
   assert (bounce.observe (0.8f, true, true, true) == GraphJumpResult::Landed);
}
