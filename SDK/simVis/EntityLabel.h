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
#ifndef SIMVIS_ENTITY_LABEL_H
#define SIMVIS_ENTITY_LABEL_H

#include <string>
#include "osg/ref_ptr"
#include "simData/DataTypes.h"
#include "simVis/LocatorNode.h"

namespace osgEarth { namespace Annotation { class LabelNode; } }
namespace simVis
{
/// Class for managing the labels
class SDKVIS_EXPORT EntityLabelNode : public LocatorNode
{
public:
  /// Constructors
  EntityLabelNode();
  explicit EntityLabelNode(simVis::Locator* locator);

  /// Update the label with the given preferences and text
  void update(const simData::CommonPrefs& commonPrefs, const std::string& text, float zOffset=0.f);

  /** Return the proper library name */
  virtual const char* libraryName() const { return "simVis"; }

  /** Return the class name */
  virtual const char* className() const { return "EntityLabelNode"; }

protected:
  virtual ~EntityLabelNode();

private:
  /** Copy constructor, not implemented or available. */
  EntityLabelNode(const EntityLabelNode&);

  osg::ref_ptr<osgEarth::Annotation::LabelNode> label_;  ///< The actual label
  simData::CommonPrefs lastCommonPrefs_;  ///< The last preferences to check for changes
  bool hasLastPrefs_; ///< Whether lastCommonPrefs_ has been set by prefs we received
  std::string lastText_; ///< The last text to check for change
};

}

#endif

