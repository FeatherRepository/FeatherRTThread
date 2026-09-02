/* Keep the SDK-owned Tiny JPEG decoder in this repository and compile it as
 * part of the GPU UI group.  The wrapper also keeps its object in the project
 * build directory instead of next to the shared SDK source. */
#include "../../../../libraries/Common/board/ports/usb/tjpgd.c"
