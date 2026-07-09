/*
 * Standalone smoke test: walks the C2 lifecycle for both Opus and AAC SW
 * components — without GStreamer involved — to isolate porting/shim bugs
 * from plugin bugs.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <C2.h>
#include <C2Component.h>
#include <C2PlatformSupport.h>

#include <cstdio>
#include <memory>

extern std::shared_ptr<C2ComponentStore> GetCodec2PlatformComponentStore();

int main(int, char**) {
  auto store = GetCodec2PlatformComponentStore();
  if (!store) { std::fprintf(stderr, "no component store\n"); return 1; }
  std::printf("Component store: %s\n", store->getName().c_str());

  for (const char* name : { "c2.android.opus.decoder", "c2.android.aac.decoder" }) {
    std::shared_ptr<C2Component> comp;
    c2_status_t s = store->createComponent(name, &comp);
    if (s != C2_OK || !comp) {
      std::fprintf(stderr, "createComponent(%s) -> %d\n", name, s);
      return 1;
    }
    if (comp->start() != C2_OK) {
      std::fprintf(stderr, "start(%s) failed\n", name);
      return 1;
    }
    comp->stop();
    comp->release();
    std::printf("OK: %s start+stop\n", name);
  }
  return 0;
}
