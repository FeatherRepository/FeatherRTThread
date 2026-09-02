/* Reuse the UI-independent persistent A/B store.  Keeping the wrapper in the
 * GPU component makes SCons place its object in build/ instead of the source
 * tree; the included implementation remains repository-owned. */
#include "../ui/feathertalk_ui_preferences_store.c"
