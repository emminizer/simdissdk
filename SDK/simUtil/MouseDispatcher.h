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
#ifndef SIMUTIL_MOUSEDISPATCHER_H
#define SIMUTIL_MOUSEDISPATCHER_H

#include <map>
#include <set>
#include <vector>
#include "osg/observer_ptr"
#include "osg/ref_ptr"
#include "simCore/Common/Common.h"
#include "simUtil/MouseManipulator.h"

namespace simVis {
  class ViewManager;
  class AddEventHandlerToViews;
}

namespace simUtil {

/**
 * Responsible for delegating mouse functionality in serial amongst several registered
 * and prioritized mouse manipulators.  Works similar to a GUIEventHandler (and uses one
 * for its implementation), but differs by providing a prioritization feature for the
 * mouse manipulators (as opposed to the built in Chain of Responsibility in OSG).
 */
class SDKUTIL_EXPORT MouseDispatcher
{
public:
  MouseDispatcher();
  virtual ~MouseDispatcher();

  /** Weight associated with the exclusive (one and only one active at a time) mouse manipulator */
  static const int EXCLUSIVE_MOUSE_WEIGHT = 25;

  /** Changes the view manager and sets up the callbacks required for intercepting the mouse */
  void setViewManager(simVis::ViewManager* viewManager);

  /** Lower weight number means the manipulator will be serviced before others with higher weight numbers. */
  void addManipulator(int weight, MouseManipulatorPtr manipulator);
  /**
   * Adds a mouse manipulator that changes the click ability.  Exclusive manipulators can become active and
   * inactive and only one is active at a time.  These manipulators are mutually exclusive; 0 or 1 is active.
   */
  void addExclusiveManipulator(MouseManipulatorPtr manipulator);
  /** Removes the manipulator from the list.  Note this should not be called from a MouseManipulator to avoid iterator invalidation. */
  void removeManipulator(MouseManipulatorPtr manipulator);

  /** Activates a single exclusive manipulator, deactivating all other mutually exclusive manipulators. */
  int activateExclusive(MouseManipulatorPtr manipulator);
  /** Deactivates an exclusive manipulator.  If it was active, then the active exclusive manipulator is set to NULL. */
  int deactivateExclusive(MouseManipulatorPtr manipulator);
  /** Retrieves the currently active exclusive manipulator; might be NULL */
  MouseManipulatorPtr activeExclusiveManipulator() const;

  /** Observable events on the Mouse Dispatcher */
  class Observer
  {
  public:
    virtual ~Observer() {}
    /** Manipulator has changed.  May be NULL. */
    virtual void activeExclusiveManipulatorChanged(MouseManipulatorPtr active, MouseManipulatorPtr oldActive) = 0;
  };

  /** Add an observer */
  void addObserver(std::shared_ptr<Observer> observer);
  /** Remove an observer */
  void removeObserver(std::shared_ptr<Observer> observer);

private:
  /** Fires the observable event */
  void fireActiveExclusiveManipulatorChanged_(MouseManipulatorPtr active, MouseManipulatorPtr oldActive);

  /** Stores a reference to the view manager */
  osg::observer_ptr<simVis::ViewManager> viewManager_;

  /** Defines the storage for weight + manipulator */
  typedef std::multimap<int, MouseManipulatorPtr> PriorityMap;
  /** Instance of a priority map on the mouse manipulators */
  PriorityMap priorityMap_;

  /** Encapsulates the GUI Event Handler in OSG */
  class EventHandler;
  /** Encapsulation of a osgGA::GUIEventHandler */
  osg::ref_ptr<EventHandler> eventHandler_;

  /** Instance of the observer of views added/deleted */
  osg::ref_ptr<simVis::AddEventHandlerToViews> viewObserver_;

  /** Stores all mutually exclusive manipulators */
  std::set<MouseManipulatorPtr> allExclusive_;
  /** Proxy that switches between the current mutually exclusive manipulator */
  std::shared_ptr<MouseManipulatorProxy> exclusiveProxy_;

  /** Observers */
  std::vector<std::shared_ptr<Observer> > observers_;
};

}

#endif /* SIMUTIL_MOUSEDISPATCHER_H */
