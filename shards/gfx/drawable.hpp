#ifndef B908F41D_8437_47C0_B3F9_FA0AE98387FC
#define B908F41D_8437_47C0_B3F9_FA0AE98387FC

#include "fwd.hpp"
#include "linalg.hpp"
#include "material.hpp"
#include "hasherxxh128.hpp"
#include "pmr/wrapper.hpp"
#include "pmr/vector.hpp"
#include <memory>
#include <vector>

namespace gfx {

namespace detail {
struct PipelineHashCollector;
}

typedef detail::DrawableProcessorPtr (*DrawableProcessorConstructor)(Context &);

struct IDrawable {
  virtual ~IDrawable() = default;

  // Duplicate self
  virtual DrawablePtr clone() const = 0;

  // Unique Id to identify this drawable
  virtual UniqueId getId() const = 0;

  // If this is a group this function should extract it's contents and return true
  virtual bool expand(shards::pmr::vector<const IDrawable *> &outDrawables) const { return false; }

  // Get the processor used to render this drawable
  virtual DrawableProcessorConstructor getProcessor() const = 0;

  // Compute hash and collect references
  // The drawable should not be modified while it is being processed by the renderer
  // After Renderer::render returns you are free to change this drawable again
  virtual void pipelineHashCollect(detail::PipelineHashCollector &PipelineHashCollector) const = 0;
};

template <typename T> inline std::shared_ptr<T> clone(const std::shared_ptr<T> &other) {
  return std::static_pointer_cast<T>(other->clone());
}

UniqueId getNextDrawableId();

struct DrawQueue {
private:
  std::vector<DrawablePtr> sharedDrawables;
  std::vector<const IDrawable *> drawables;
  bool autoClear{};

public:
  // Adds a managed drawable, automatically kept alive until the renderer is done with it
  void add(const DrawablePtr &drawable) {
    sharedDrawables.push_back(drawable);
    drawables.push_back(drawable.get());
  }

  // Add an external drawable, you are responsible for keeping the object alive until it has been submitted to the renderer
  void add(const IDrawable &drawable) { drawables.push_back(&drawable); }

  void clear() {
    drawables.clear();
    sharedDrawables.clear();
  }

  bool isAutoClear() const { return autoClear; }
  void setAutoClear(bool autoClear) { this->autoClear = autoClear; }

  const std::vector<DrawablePtr> &getSharedDrawables() const { return sharedDrawables; }
  const std::vector<const IDrawable *> &getDrawables() const { return drawables; }
};

} // namespace gfx

#endif /* B908F41D_8437_47C0_B3F9_FA0AE98387FC */
