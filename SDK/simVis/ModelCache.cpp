/* -*- mode: c++ -*- */
/****************************************************************************
 *****                                                                  *****
 *****                   Classification: UNCLASSIFIED                   *****
 *****                    Classified By:                                *****
 *****                    Declassify On:                                *****
 *****                                                                  *****
 ****************************************************************************
 *
 *
 * Developed by: Naval Research Laboratory, Tactical Electronic Warfare Div.
 *               EW Modeling & Simulation, Code 5773
 *               4555 Overlook Ave.
 *               Washington, D.C. 20375-5339
 *
 * License for source code at https://simdis.nrl.navy.mil/License.aspx
 *
 * The U.S. Government retains all rights to use, duplicate, distribute,
 * disclose, or release this software.
 *
 */
#include <cassert>
#include "osg/LOD"
#include "osg/Node"
#include "osg/NodeVisitor"
#include "osg/ProxyNode"
#include "osg/Sequence"
#include "osg/ShapeDrawable"
#include "osg/ValueObject"
#include "osgSim/DOFTransform"
#include "osgSim/MultiSwitch"
#include "osgUtil/Optimizer"
#include "osgEarth/NodeUtils"
#include "osgEarth/Registry"
#include "osgEarth/ShaderGenerator"
#include "osgEarthAnnotation/AnnotationUtils"
#include "simNotify/Notify.h"
#include "simCore/String/Utils.h"
#include "simVis/ClockOptions.h"
#include "simVis/Utils.h"
#include "simVis/ModelCache.h"

namespace simVis {

/** Extension to use for the pseudo-loader for model cache */
static const std::string MODEL_LOADER_EXT = "simvis_modelcache";
/** Key to use for User Values to flag whether loaded node should be cached */
static const std::string CACHE_HINT_KEY = "CacheHint";
/** Key to use for User Values to flag whether loaded node is an image */
static const std::string IMAGE_HINT_KEY = "ImageHint";

/** Local helper visitor to add a given callback to all sequences in the graph */
class AddUpdateCallbackToSequence : public osg::NodeVisitor
{
public:
  explicit AddUpdateCallbackToSequence(osg::Callback* callback)
    : NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
      callback_(callback)
  {
  }

  void apply(osg::Sequence& sequence)
  {
    sequence.addUpdateCallback(callback_);
    traverse(sequence);
  }

private:
  osg::Callback* callback_;
};

////////////////////////////////////////////////////////////////////////////

/** Visitor that clears out the render bins of all nodes; used to fix up a regularly occurring alpha issue */
class SetRenderBinsToInherit : public osg::NodeVisitor
{
public:
  SetRenderBinsToInherit()
    : NodeVisitor(TRAVERSE_ALL_CHILDREN)
  {
  }
  virtual void apply(osg::Node& node)
  {
    osg::StateSet* ss = node.getStateSet();
    if (ss)
      ss->setRenderBinToInherit();
    traverse(node);
  }
};

////////////////////////////////////////////////////////////////////////////

/** Options class that holds onto the Clock and SequenceTimeUpdater from the Model Cache. */
class ModelCacheLoaderOptions : public osgDB::ReaderWriter::Options
{
public:
  META_Object(simVis, ModelCacheLoaderOptions);

  /** Default constructor */
  ModelCacheLoaderOptions()
    : boxWhenNotFound(false),
      addLodNode(true),
      runShaderGenerator(true),
      clock(NULL)
  {
    optimizeFlags = osgUtil::Optimizer::DEFAULT_OPTIMIZATIONS |
      osgUtil::Optimizer::VERTEX_PRETRANSFORM |
      osgUtil::Optimizer::VERTEX_POSTTRANSFORM |
      osgUtil::Optimizer::INDEX_MESH;
  }

  /** OSG Copy constructor; required for use with database pager options. */
  ModelCacheLoaderOptions(const ModelCacheLoaderOptions& rhs, const osg::CopyOp& copyOp)
    : Options(rhs, copyOp),
      boxWhenNotFound(rhs.boxWhenNotFound),
      addLodNode(rhs.addLodNode),
      optimizeFlags(rhs.optimizeFlags),
      runShaderGenerator(rhs.runShaderGenerator),
      clock(rhs.clock),
      sequenceTimeUpdater(rhs.sequenceTimeUpdater)
  {
  }

  /**
   * True: returns a box model if the model was not found.
   * False: return FILE_NOT_HANDLED as normal.
   * This is useful because osg::ProxyNode will continuously request the database pager for the model
   * until some node is returned.  So if requesting a not-found node and FILE_NOT_HANDLED is returned,
   * then the node is continuously requested until the ProxyNode is deleted.  There is no notification
   * of this failure.  To remedy this, asynchronous mode can set this flag to true to return a box
   * when the model cannot be found.
   */
  bool boxWhenNotFound;
  /** Set true to create an LOD node that swaps out when item is too small on screen. */
  bool addLodNode;
  /** Change the flags sent to optimizer.  Set to 0 to disable optimization. */
  unsigned int optimizeFlags;
  /** Set true to run the osgEarth shader generator on the resulting node. */
  bool runShaderGenerator;
  /** Clock object used for SIMDIS Media Player 2 playlist nodes. */
  simCore::Clock* clock;
  /** Pointer to the class that fixes osg::Sequence; see simVis::Registry::sequenceTimeUpdater_. */
  osg::observer_ptr<SequenceTimeUpdater> sequenceTimeUpdater;

protected:
  virtual ~ModelCacheLoaderOptions() {}
};

////////////////////////////////////////////////////////////////////////////

/** Pseudo-loader for ModelCache that does the heavy lifting, including reading node and optimizing it. */
class ModelCacheLoader : public osgDB::ReaderWriter
{
public:
  META_Object(simVis, ModelCacheLoader);

  /** Default constructor */
  ModelCacheLoader()
  {
    supportsExtension(MODEL_LOADER_EXT, "simVis Model Cache Pseudo-Loader");
  }
  /** OSG copy constructor, needed for META_Object. */
  ModelCacheLoader(const ModelCacheLoader& copy, const osg::CopyOp& copyOp)
    : ReaderWriter(copy, copyOp)
  {
    // Create a box model as a placeholder for invalid model
    osg::Geode* geode = new osg::Geode();
    geode->addDrawable(new osg::ShapeDrawable(new osg::Box()));
    boxNode_ = geode;
  }

  /** Called by OSG when a filename is requested to be read into a node. */
  virtual ReadResult readNode(const std::string& filename, const osgDB::ReaderWriter::Options* options) const
  {
    const std::string ext = osgDB::getLowerCaseFileExtension(filename);
    if (!acceptsExtension(ext))
      return ReadResult::FILE_NOT_HANDLED;

    // Strip the extension
    const auto* mcOpts = dynamic_cast<const ModelCacheLoaderOptions*>(options);
    const std::string tmpName = osgDB::getNameLessExtension(filename);
    if (tmpName.empty() && (!mcOpts || !mcOpts->boxWhenNotFound))
      return ReadResult::FILE_NOT_HANDLED;
    return readNode_(tmpName, mcOpts);
  }

  /** Sets the box node to use for invalid models */
  void setBoxNode(osg::Node* boxNode)
  {
    boxNode_ = boxNode;
  }

private:
  /** Helper method reads the raw filename and respects the incoming options. */
  ReadResult readNode_(const std::string& filename, const ModelCacheLoaderOptions* options) const
  {
    // ref_ptr that will hold the return value
    osg::ref_ptr<osg::Node> result;
    // Only cache icons re-usable between scenarios
    bool cacheIt = true;
    bool isImage = simVis::isImageFile(filename);
    if (isImage)
      result = readImageNode_(filename, options, cacheIt);
    else // is model
      result = readModelNode_(filename, options);

    // Deal with problems from loading the requested filename
    if (!result.valid())
    {
      if (!options || !options->boxWhenNotFound)
        return ReadResult::FILE_NOT_HANDLED;
      // Use box node for a placeholder for invalid model
      result = boxNode_;
      isImage = false;
    }

    // Apply post-load options
    applyPostLoadOptions_(result, options);
    // Save the is-image and cache flag hints
    result->setUserValue(IMAGE_HINT_KEY, isImage);
    result->setUserValue(CACHE_HINT_KEY, cacheIt);

    return result.release();
  }

  /** Helper method that applies the various post-read-node operations to a node. */
  void applyPostLoadOptions_(osg::ref_ptr<osg::Node>& result, const ModelCacheLoaderOptions* options) const
  {
    if (!result || !options)
      return;

    // Set up an LOD for performance's sake that eliminates the object from drawing if eye is too far.
    if (options->addLodNode)
    {
      osg::ref_ptr<osg::LOD> lod = new osg::LOD;
      // Use a pixel-size LOD.  Range LOD scales relative to eye distance, but models that get distorted
      // significantly in only 2 dimensions will have significant LOD issues with that approach.
      lod->setRangeMode(osg::LOD::PIXEL_SIZE_ON_SCREEN);
      lod->addChild(result.get(), 2.f, std::numeric_limits<float>::max());
      result = lod;
    }

    // Perform vertex cache ordering optimization.
    if (options->optimizeFlags != 0)
    {
      osgUtil::Optimizer o;
      o.optimize(result, options->optimizeFlags);
    }

    // generate shaders.
    if (options->runShaderGenerator)
    {
      osg::ref_ptr<osgEarth::StateSetCache> stateCache = new osgEarth::StateSetCache();
      osgEarth::Registry::shaderGenerator().run(result.get(), stateCache.get());
    }
  }

  /** Helper method to process the filename into an image.  Also handles SIMDIS MP2 files. */
  osg::ref_ptr<osg::Node> readImageNode_(const std::string& filename, const ModelCacheLoaderOptions* options, bool& cacheIt) const
  {
    osg::ref_ptr<osg::Node> result;

    // For an image, build a "billboard" that will always face the user
    // and auto-scale to the screen.
    using namespace osgEarth::Annotation;

    osg::ref_ptr<osg::Image> image = osgDB::readRefImageFile(filename);
    if (image)
    {
      // create the geometry representing the icon:
      osg::Geometry* geom = AnnotationUtils::createImageGeometry(
        image.get(),
        osg::Vec2s(0, 0), // pixel offsets from center
        0,                // texture image unit
        0.0,              // heading
        1.0);             // scale

      osg::Geode* geode = new osg::Geode();
      geode->addDrawable(geom);
      result = geode;
    }
    else
    {
      // See if it is an MP2 file

      // Need a clock to drive the time dependent icons; failure means missing a call to setClock
      assert(options && options->clock);
      if (options && options->clock)
      {
        osg::ref_ptr<simVis::ClockOptions> cOpts = new simVis::ClockOptions(options->clock);
        result = osgDB::readRefNodeFile(filename, cOpts.get());
        // The time dependent icons MUST not be cached to prevent them from leaking into the next scenario which may or may not need them
        cacheIt = false;
      }
    }

    // Apply rendering hints to the new node, appropriate for 2D images
    if (result.valid())
    {
      osg::StateSet* stateSet = result->getOrCreateStateSet();
      // As per SIMSDK-157, blending needs to be on to avoid jaggies
      stateSet->setMode(GL_BLEND, osg::StateAttribute::ON);
    }
    return result;
  }

  /** Helper method to process the filename into an model. */
  osg::ref_ptr<osg::Node> readModelNode_(const std::string& filename, const ModelCacheLoaderOptions* options) const
  {
    // Convert the URI to Windows backslashes if needed
    std::string localUri = simCore::toNativeSeparators(filename);
    osg::ref_ptr<osg::Node> result = osgDB::readRefNodeFile(localUri);

    // Need to apply a sequence time fix for osg::Sequence to deal with decreasing simulation times
    if (result.valid())
    {
      osg::ref_ptr<SequenceTimeUpdater> seqUpdater;
      if (options && options->sequenceTimeUpdater.lock(seqUpdater))
      {
        AddUpdateCallbackToSequence fixSequenceTimeUpdates(seqUpdater.get());
        result->accept(fixSequenceTimeUpdates);
      }

      // Set all render bins for the loaded model to Inherited.  This allows us to later on put
      // the model into a traversal order bin.  This helps with model display of alpha textures.
      SetRenderBinsToInherit setRenderBinsToInherit;
      result->accept(setRenderBinsToInherit);
    }
    return result;
  }

  osg::ref_ptr<osg::Node> boxNode_;
};

REGISTER_OSGPLUGIN(simvis_modelcache, ModelCacheLoader)

////////////////////////////////////////////////////////////////////////////

/**
 * Asynchronous loading is handled by this node, which must be in the scene graph.  The asynchronous
 * loading works by creating an osg::ProxyNode child.  During traverse(), we detect which proxy nodes
 * have finished loading.  We then remove the ProxyNode to prevent successive requests, and call any
 * callbacks that have been registered for that URI.
 *
 * osg::ProxyNode relies on the cull traversal's database pager to load the model.  ProxyNode will
 * only traverse if it's in the scene.  Thus, this parent must be in the scene too, else the model
 * will never load.
 */
class ModelCache::LoaderNode : public osg::Group
{
public:
  typedef std::vector<osg::ref_ptr<ModelCache::ModelReadyCallback> > CallbackVector;

  /** Initializes the Loader Node */
  LoaderNode()
    : cache_(NULL)
  {
  }

  /** Change the cache pointer.  This is useful because the Loader Node might outlive the ModelCache. */
  void setCache(simVis::ModelCache* cache)
  {
    cache_ = cache;
  }

  /** Clears out all requests */
  void clear()
  {
    requests_.clear();
    const unsigned int numChildren = getNumChildren();
    if (numChildren)
      removeChildren(0, numChildren);
  }

  /** Requests that a node be loaded asynchronously.  Callback will be executed when completed. */
  void addRequest(const std::string& uri, ModelCache::ModelReadyCallback* callback)
  {
    // Assertion failure means that the loader node will fail.  The loader node requires a
    // parent in the scene to work, because the background loading uses the OSG database
    // pager facilities, available primarily in the cull traversal.  This provides the
    // synchronization point as well.  So if we have no parents, traverse is never called,
    // so nodes never get loaded.  Developer error.  simVis::SceneManager attaches the
    // default (simVis::Registry) model cache to the scene.
    assert(getNumParents());

    // Create a new request record
    CallbackVector& callbacks = requests_[uri];
    callbacks.push_back(callback);
    // Return early if there's already another active request
    if (callbacks.size() != 1)
      return;

    // Set up an options struct for the pseudo loader
    osg::ref_ptr<ModelCacheLoaderOptions> opts = new ModelCacheLoaderOptions;
    opts->clock = cache_->clock_;
    opts->sequenceTimeUpdater = cache_->sequenceTimeUpdater_.get();
    // Need to return something or proxy never succeeds and keeps issuing searches
    opts->boxWhenNotFound = true;

    // Create a proxy node to load the model
    osg::ref_ptr<osg::ProxyNode> proxy = new osg::ProxyNode;
    proxy->setFileName(0, uri + "." + MODEL_LOADER_EXT);
    proxy->setDatabaseOptions(opts.get());
    proxy->setUserValue("uri", uri);
    // Culling needs to be off for the traverse() to hit
    proxy->setCullingActive(false);
    addChild(proxy);
  }

  virtual void traverse(osg::NodeVisitor& nv)
  {
    osg::Group::traverse(nv);

    // Check for proxy node children with children -- they just finished loading.
    unsigned int childIndex = 0;
    while (childIndex < getNumChildren())
    {
      osg::ref_ptr<osg::ProxyNode> proxy = dynamic_cast<osg::ProxyNode*>(getChild(childIndex));
      // Assertion failure means scene corruption or someone is messing with our nodes.
      assert(proxy.valid());
      if (!proxy.valid())
      {
        ++childIndex;
        continue;
      }

      // Detect children
      if (proxy->getNumChildren())
      {
        osg::ref_ptr<osg::Node> loaded = proxy->getChild(0);
        // Shouldn't be able to have NULLs here.  Investigate what to do if this triggers.
        assert(loaded.valid());
        std::string uri;
        if (!proxy->getUserValue("uri", uri))
        {
          // Load was requested, but didn't have a URI.  Someone might be messing with our nodes.
          assert(0);
        }
        // Alert callbacks
        fireLoadFinished_(uri, loaded.get());

        // Remove the proxy so it doesn't show up in the scene
        removeChild(childIndex, 1);
      }
      ++childIndex;
    }
  }

private:
  /** Loading on a URI completed.  Alert everyone who cares. */
  void fireLoadFinished_(const std::string& uri, const osg::ref_ptr<osg::Node>& node)
  {
    auto requestIter = requests_.find(uri);
    // Failure here means we got a URI we didn't expect, and don't know who to tell.  Failure
    // in recording URIs, or some other external problem likely.
    assert(requestIter != requests_.end());
    if (requestIter == requests_.end())
      return;
    // Remove the request immediately, saving the callbacks
    const auto callbacks = requestIter->second;
    requests_.erase(requestIter);

    // Retrieve the hints for caching and image flag
    bool isImage = false;
    bool cacheIt = false;
    if (node)
    {
      node->getUserValue(IMAGE_HINT_KEY, isImage);
      node->getUserValue(CACHE_HINT_KEY, cacheIt);
    }
    const bool isArticulated = ModelCache::isArticulated(node.get());

    // Respect the cache hint
    if (cacheIt)
      cache_->saveToCache_(uri, node.get(), isArticulated, isImage);

    // Pass the new node to everyone who is listening
    for (auto i = callbacks.begin(); i != callbacks.end(); ++i)
    {
      // Articulated models need to clone
      if (isArticulated && !cache_->getShareArticulatedIconModels())
      {
        osg::ref_ptr<osg::Node> copy = osg::clone(node.get(), osg::CopyOp::DEEP_COPY_NODES);
        (*i)->loadFinished(copy, isImage, uri);
      }
      else
        (*i)->loadFinished(node, isImage, uri);

      // TODO: Determine correct logic for loading models from Video Icons if loading 2 of the
      // same at the same time.
    }
  }

  /** Pointer back to our parent cache */
  simVis::ModelCache* cache_;
  /** Maps the URI to the vector of callbacks to use when the request is ready */
  std::map<std::string, CallbackVector> requests_;
};

////////////////////////////////////////////////////////////////////////////

ModelCache::ModelCache()
  : shareArticulatedModels_(false),
    clock_(NULL),
    asyncLoader_(new LoaderNode)
{
  asyncLoader_->setCache(this);

  // Create a box model as a placeholder for invalid model
  osg::Geode* geode = new osg::Geode();
  geode->addDrawable(new osg::ShapeDrawable(new osg::Box()));
  boxNode_ = geode;

  // Share the box model with the loader, to reduce number of geometry in the scene and increase reuse
  ModelCacheLoader* cacheRw = dynamic_cast<ModelCacheLoader*>(osgDB::Registry::instance()->getReaderWriterForExtension(MODEL_LOADER_EXT));
  // Assertion failure means we're going to never be able to read/write model files.  Check REGISTER_OSGPLUGIN
  assert(cacheRw);
  if (cacheRw)
    cacheRw->setBoxNode(boxNode_);
}

ModelCache::~ModelCache()
{
  // Clear the cache in the loader to avoid stale pointers
  asyncLoader_->clear();
  asyncLoader_->setCache(NULL);
}

osg::Node* ModelCache::getOrCreateIconModel(const std::string& uri, bool* pIsImage)
{
  // first check the cache.
  auto i = cache_.find(uri);
  if (i != cache_.end())
  {
    const Entry& entry = i->second;
    if (pIsImage)
      *pIsImage = entry.isImage_;

    if (entry.isArticulated_ && !shareArticulatedModels_)
    {
      // clone nodes so we get independent articulations
      return osg::clone(entry.node_.get(), osg::CopyOp::DEEP_COPY_NODES);
    }

    // shared scene graph:
    return entry.node_.get();
  }

  // Set up an options struct for the pseudo loader
  osg::ref_ptr<ModelCacheLoaderOptions> opts = new ModelCacheLoaderOptions;
  opts->clock = clock_;
  opts->sequenceTimeUpdater = sequenceTimeUpdater_.get();
  // Farm off to the pseudo-loader
  osg::ref_ptr<osg::Node> result = osgDB::readRefNodeFile(uri + "." + MODEL_LOADER_EXT, opts.get());
  if (!result)
    return NULL;

  // Store the image hint
  bool isImage = false;
  result->getUserValue(IMAGE_HINT_KEY, isImage);
  if (pIsImage)
    *pIsImage = isImage;

  // Respect the cache hint
  bool cacheIt = false;
  result->getUserValue(CACHE_HINT_KEY, cacheIt);
  if (cacheIt)
    saveToCache_(uri, result.get(), ModelCache::isArticulated(result.get()), isImage);

  return result.release();
}

void ModelCache::saveToCache_(const std::string& uri, osg::Node* node, bool isArticulated, bool isImage)
{
  Entry& entry = cache_[uri];
  entry.node_ = node;
  entry.isImage_ = isImage;
  entry.isArticulated_ = isArticulated;
}

void ModelCache::asyncLoad(const std::string& uri, ModelReadyCallback* callback)
{
  // Save the callback in a ref_ptr for memory
  osg::ref_ptr<ModelReadyCallback> refCallback = callback;

  // first check the cache
  const auto cacheIter = cache_.find(uri);
  if (cacheIter != cache_.end())
  {
    // If the callback is valid, then pass the model back immediately.  It's possible the
    // callback might not be valid in cases where someone is attempting to preload icons
    // for the sake of performance.  In that case we just return early because it's loaded.
    if (refCallback.valid())
    {
      const Entry& entry = cacheIter->second;
      osg::ref_ptr<osg::Node> node = entry.node_.get();
      // clone articulated nodes so we get independent articulations
      if (entry.isArticulated_ && !shareArticulatedModels_)
        node = osg::clone(entry.node_.get(), osg::CopyOp::DEEP_COPY_NODES);
      refCallback->loadFinished(node, entry.isImage_, uri);
    }

    return;
  }

  // Queue up the request with the async loader
  asyncLoader_->addRequest(uri, callback);
}

void ModelCache::setShareArticulatedIconModels(bool value)
{
  shareArticulatedModels_ = value;
}

bool ModelCache::getShareArticulatedIconModels() const
{
  return shareArticulatedModels_;
}

void ModelCache::setClock(simCore::Clock* clock)
{
  clock_ = clock;
}

simCore::Clock* ModelCache::getClock() const
{
  return clock_;
}

void ModelCache::setSequenceTimeUpdater(SequenceTimeUpdater* sequenceTimeUpdater)
{
  sequenceTimeUpdater_ = sequenceTimeUpdater;
}

void ModelCache::clear()
{
  asyncLoader_->clear();
  cache_.clear();
}

bool ModelCache::isArticulated(osg::Node* node)
{
  osgSim::DOFTransform* dof = osgEarth::findTopMostNodeOfType<osgSim::DOFTransform>(node);
  if (dof)
    return true;
  osgSim::MultiSwitch* ms = osgEarth::findTopMostNodeOfType<osgSim::MultiSwitch>(node);
  if (ms)
    return true;
  return osgEarth::findTopMostNodeOfType<osg::Sequence>(node) != NULL;
}

osg::Node* ModelCache::asyncLoaderNode() const
{
  return asyncLoader_.get();
}

osg::Node* ModelCache::boxNode() const
{
  return boxNode_.get();
}

////////////////////////////////////////////////////////////////////////////

ReplaceChildReadyCallback::ReplaceChildReadyCallback(osg::Group* parent, unsigned int childIndex)
  : parent_(parent),
    childIndex_(childIndex)
{
}

void ReplaceChildReadyCallback::loadFinished(const osg::ref_ptr<osg::Node>& model, bool isImage, const std::string& filename)
{
  assert(parent_.valid() && parent_->getNumChildren() == 1);
  assert(model.valid());
  // Make sure parent is still around
  osg::ref_ptr<osg::Group> parent;
  if (!parent_.lock(parent))
    return;

  // Replace the model with a box if needed
  osg::ref_ptr<osg::Node> newModel = model;
  if (!newModel.valid())
  {
    osg::Geode* geode = new osg::Geode();
    geode->addDrawable(new osg::ShapeDrawable(new osg::Box()));
    newModel = geode;
  }

  // Replace the requested entry
  if (childIndex_ < parent->getNumChildren())
    parent->replaceChild(parent->getChild(childIndex_), newModel);
  else
    parent->addChild(newModel);
}

}