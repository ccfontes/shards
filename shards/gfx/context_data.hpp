#ifndef GFX_CONTEXT_DATA
#define GFX_CONTEXT_DATA

#include <cassert>
#include <memory>
#include <mutex>

namespace gfx {

template <typename T> struct TWithContextData;
struct Context;

// Can be assigned to a context to be notified when GPU resources should be released
// NOTE: Your destructor should manually call releaseContextDataConditional!!!
struct ContextData : public std::enable_shared_from_this<ContextData> {
private:
  Context *context = nullptr;

public:
  static std::recursive_mutex globalMutex;

  ContextData() = default;
  virtual ~ContextData() = default;

  void releaseContextDataConditional();
  Context &getContext() {
    assert(context);
    return *context;
  }
  bool isBoundToContext() const { return context; }

protected:
  virtual void releaseContextData() = 0;

protected:
  template <typename T> friend struct TWithContextData;
  void bindToContext(Context &context);
  void unbindFromContext();
};

// Manages initialization/cleanup of context-specific data T attached to this object
template <typename T> struct TWithContextData {
  std::shared_ptr<T> contextData;

  T &createContextDataConditional(Context &context) {
    ContextData::globalMutex.lock();

    // Create or reinitialize context data
    if (!contextData || !contextData->context) {
      contextData = std::make_shared<T>();
      contextData->bindToContext(context);
      initContextData(context, *contextData.get());
    } else {
      assert(&contextData->getContext() == &context);
    }
    updateContextData(context, *contextData.get());

    ContextData::globalMutex.unlock();

    return *contextData.get();
  }

protected:
  virtual void initContextData(Context &context, T &contextData) = 0;
  virtual void updateContextData(Context &context, T &contextData) {}
};

} // namespace gfx

#endif // GFX_CONTEXT_DATA
