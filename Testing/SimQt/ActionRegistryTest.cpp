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
#include <iostream>
#include <QAction>
#include <QApplication>
#include <QPushButton>
#include "simCore/Common/SDKAssert.h"
#include "simVis/Headless.h"
#include "simQt/ActionRegistry.h"

using namespace simQt;

namespace
{

struct NewRegistry
{
  ActionRegistry* reg;
  QAction* superform;
  QAction* rangeTool;
  QAction* views;
  QAction* pluginManager;
  QAction* help;
  NewRegistry()
   : reg(new ActionRegistry(NULL)),
     superform(new QAction("superform", NULL)),
     rangeTool(new QAction("rangeTool...", NULL)),
     views(new QAction("views...", NULL)),
     pluginManager(new QAction("pluginManager...", NULL)),
     help(new QAction("help...", NULL))
  {
    superform->setShortcuts(QList<QKeySequence>() << QKeySequence("Alt+S") << QKeySequence("Ctrl+S"));
    reg->registerAction("Tools", "SuperForm", superform);
    reg->registerAction("Tools", "Range Tool", rangeTool);
    reg->registerAction("View", "Views", views);
    reg->registerAction("Plugins", "Plugin Manager", pluginManager);
    reg->registerAction("Help", "Help", help);
  }
  ~NewRegistry()
  {
    delete reg;
    delete superform;
    delete rangeTool;
    delete views;
    delete pluginManager;
    delete help;
  }

private:
  /** Not implemented */
  NewRegistry(const NewRegistry& noCopyConstructor);
};

int testFind()
{
  int rv = 0;

  NewRegistry reg;
  rv += SDK_ASSERT(!reg.reg->actions().empty());

  Action* action = reg.reg->findAction("Views");
  rv += SDK_ASSERT(action != NULL);
  if (action != NULL)
  {
    rv += SDK_ASSERT(action->description() == "Views");
    rv += SDK_ASSERT(action->hotkeys().empty());
  }
  action = reg.reg->findAction("View");
  rv += SDK_ASSERT(action == NULL);

  // Re-register should not work; throw an exception for testing?
  //Action* badAction = reg.reg->registerAction("Unknown", "Views", reg.superform);
  //rv += SDK_ASSERT(badAction != NULL);
  //rv += SDK_ASSERT(badAction->action() != reg.superform);
  //rv += SDK_ASSERT(badAction->group() != "Unknown");

  // Re-register of same action with different name should also fail; throw an exception?
  //Action* badAction2 = reg.reg->registerAction("Another", "Another", reg.superform);
  //if (badAction2 != NULL)
  //{
  //  rv += SDK_ASSERT(badAction2->action() == reg.superform);
  //  rv += SDK_ASSERT(badAction2->description() != "Another");
  //  rv += SDK_ASSERT(badAction2->group() != "Another");
  //}

  action = reg.reg->findAction(QKeySequence("Alt+S"));
  rv += SDK_ASSERT(action != NULL);
  if (action != NULL)
    rv += SDK_ASSERT(action->description() == "SuperForm");
  action = reg.reg->findAction(QKeySequence("Ctrl+S"));
  rv += SDK_ASSERT(action != NULL);
  if (action != NULL)
  {
    rv += SDK_ASSERT(action->description() == "SuperForm");
    rv += SDK_ASSERT(action->hotkeys().size() == 2);
  }

  // Remove hotkey from SuperForm and re-search
  reg.reg->removeHotKey(action, 0); // should remove Alt+S
  action = reg.reg->findAction(QKeySequence("Alt+S"));
  rv += SDK_ASSERT(action == NULL);
  action = reg.reg->findAction(QKeySequence("Ctrl+S"));
  rv += SDK_ASSERT(action != NULL);
  if (action != NULL)
    rv += SDK_ASSERT(action->hotkeys().size() == 1);

  // Search for nonexistent hotkey
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Q")) == NULL);

  return rv;
}

int testSetHotKey()
{
  int rv = 0;
  NewRegistry reg;
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("V")) == NULL);
  rv += SDK_ASSERT(reg.reg->findAction("Views") != NULL);
  rv += SDK_ASSERT(reg.reg->setHotKey(reg.reg->findAction("Views"), QKeySequence("V")) == 0);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("V")) != NULL);
  rv += SDK_ASSERT(reg.reg->setHotKey(NULL, QKeySequence("V")) != 0);
  Action* views = reg.reg->findAction(QKeySequence("V"));
  rv += SDK_ASSERT(views != NULL);
  if (views)
    rv += SDK_ASSERT(views->description() == "Views");

  // Test an override
  reg.reg->setHotKey(views, QKeySequence("Alt+S"));
  views = reg.reg->findAction(QKeySequence("Alt+S"));
  rv += SDK_ASSERT(views != NULL);
  if (views)
  {
    rv += SDK_ASSERT(views->description() == "Views");
    rv += SDK_ASSERT(views->hotkeys().size() == 1);
  }
  return rv;
}

bool hasKey(const NewRegistry& reg, const QString& desc, const QString& hotkey)
{
  Action* act = reg.reg->findAction(QKeySequence(hotkey));
  return act != NULL && act->description() == desc;
}

int testAddHotKey()
{
  int rv = 0;
  NewRegistry reg;

  // Did action exist before add? (most interested in the "no" case; this covers the "yes" cases)
  reg.reg->addHotKey("Range Tool", QKeySequence("A"));
  rv += SDK_ASSERT(hasKey(reg, "Range Tool", "A"));
  reg.reg->addHotKey("Range Tool", QKeySequence("B"));
  rv += SDK_ASSERT(hasKey(reg, "Range Tool", "A"));
  rv += SDK_ASSERT(hasKey(reg, "Range Tool", "B"));
  reg.reg->addHotKey("SuperForm", QKeySequence("C"));
  rv += SDK_ASSERT(hasKey(reg, "SuperForm", "C"));
  rv += SDK_ASSERT(hasKey(reg, "SuperForm", "Alt+S"));
  rv += SDK_ASSERT(hasKey(reg, "SuperForm", "Ctrl+S"));

  // Now look at the action-doesn't-exist cases...

  // case 1: action has no shortcuts during add
  QAction test1("test1", NULL);
  reg.reg->addHotKey("test1", QKeySequence("D"));
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("D")) == NULL);
  reg.reg->registerAction("test1", "test1", &test1);
  rv += SDK_ASSERT(hasKey(reg, "test1", "D"));

  // case 2: action has shortcuts during add
  QAction test2("test2", NULL);
  reg.reg->addHotKey("test2", QKeySequence("E"));
  test2.setShortcut(QKeySequence("F"));
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("E")) == NULL);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("F")) == NULL);
  reg.reg->registerAction("test2", "test2", &test2);
  rv += SDK_ASSERT(hasKey(reg, "test2", "E"));
  rv += SDK_ASSERT(!hasKey(reg, "test2", "F"));

  // case 3: action has conflicting shortcuts during add
  QAction test3("test3", NULL);
  reg.reg->addHotKey("test3", QKeySequence("G"));
  test3.setShortcut(QKeySequence("E")); // Belongs to test2
  rv += SDK_ASSERT(hasKey(reg, "test2", "E"));
  Action* t3act = reg.reg->registerAction("test3", "test3", &test3);
  rv += SDK_ASSERT(hasKey(reg, "test2", "E"));
  rv += SDK_ASSERT(hasKey(reg, "test3", "G"));
  rv += SDK_ASSERT(t3act->hotkeys().size() == 1); // only G

  // case 4: addAction's hotkey ends up being in use already
  QAction test4("test4", NULL);
  reg.reg->addHotKey("test4", QKeySequence("G"));
  rv += SDK_ASSERT(hasKey(reg, "test3", "G"));
  reg.reg->registerAction("test4", "test4", &test4);
  rv += SDK_ASSERT(hasKey(reg, "test3", "G"));
  rv += SDK_ASSERT(t3act->hotkeys().size() == 1); // only G, still

  // Test remove action while we're at it, since there's a potential for crashing here due
  // to the out of order destruction.  Note the intentional excessive checking for side effects
  int oldSize = reg.reg->actions().size();
  rv += SDK_ASSERT(hasKey(reg, "test1", "D"));
  rv += SDK_ASSERT(reg.reg->removeAction("test1") == 0);
  rv += SDK_ASSERT(!hasKey(reg, "test1", "D"));
  rv += SDK_ASSERT(reg.reg->actions().size() == oldSize - 1);
  rv += SDK_ASSERT(hasKey(reg, "test2", "E"));
  rv += SDK_ASSERT(reg.reg->removeAction("test2") == 0);
  rv += SDK_ASSERT(!hasKey(reg, "test2", "E"));
  rv += SDK_ASSERT(hasKey(reg, "test3", "G"));
  rv += SDK_ASSERT(reg.reg->removeAction("test3") == 0);
  rv += SDK_ASSERT(reg.reg->removeAction("test4") == 0);
  rv += SDK_ASSERT(reg.reg->removeAction("test5") != 0);

  // Re-add the test3 and make sure its hotkeys were saved
  test3.setShortcuts(QList<QKeySequence>()); // make sure it's empty and coming from the action registry
  rv += SDK_ASSERT(!hasKey(reg, "test3", "G"));
  t3act = reg.reg->registerAction("test3", "test3", &test3);
  rv += SDK_ASSERT(!hasKey(reg, "test2", "E"));
  rv += SDK_ASSERT(hasKey(reg, "test3", "G"));
  rv += SDK_ASSERT(t3act->hotkeys().size() == 1); // only G

  // Re-remove it
  rv += SDK_ASSERT(reg.reg->removeAction("test3") == 0);
  rv += SDK_ASSERT(reg.reg->removeAction("test3") != 0);

  return rv;
}

int testExecute()
{
  int rv = 0;
  NewRegistry reg;
  QPushButton testButton;
  testButton.setCheckable(true);
  QAction action("exec", NULL);
  QObject::connect(&action, SIGNAL(triggered()), &testButton, SLOT(toggle()));
  rv += SDK_ASSERT(!testButton.isChecked());
  action.trigger();
  rv += SDK_ASSERT(testButton.isChecked());
  action.trigger();
  rv += SDK_ASSERT(!testButton.isChecked());
  // Test using registry
  Action* act = reg.reg->registerAction("test", "test", &action);
  rv += SDK_ASSERT(reg.reg->execute("test") == 0);
  rv += SDK_ASSERT(testButton.isChecked());
  rv += SDK_ASSERT(reg.reg->execute("foobar") != 0);
  rv += SDK_ASSERT(testButton.isChecked());
  act->execute();
  rv += SDK_ASSERT(!testButton.isChecked());
  return rv;
}

int testMemento()
{
  int rv = 0;
  NewRegistry reg;
  ActionRegistry::SettingsMemento* defaultSettings = reg.reg->createMemento();
  rv += SDK_ASSERT(defaultSettings != NULL);
  rv += SDK_ASSERT(reg.superform->shortcuts().size() == 2);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Alt+S"))->action() == reg.superform);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Q")) == NULL);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("R")) == NULL);
  // Make some changes
  rv += SDK_ASSERT(0 == reg.reg->addHotKey("Views", QKeySequence("Q")));
  rv += SDK_ASSERT(0 == reg.reg->addHotKey("Plugin Manager", QKeySequence("Alt+S")));
  rv += SDK_ASSERT(reg.superform->shortcuts().size() == 1);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Alt+S"))->action() == reg.pluginManager);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Q"))->action() == reg.views);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("R")) == NULL);
  // Save a new memento
  ActionRegistry::SettingsMemento* newSettings = reg.reg->createMemento();
  // Restore last one
  defaultSettings->restore(*reg.reg);
  // Test the original conditions again
  rv += SDK_ASSERT(reg.superform->shortcuts().size() == 2);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Alt+S"))->action() == reg.superform);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Q")) == NULL);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("R")) == NULL);
  // Test restoring the new one
  newSettings->restore(*reg.reg);
  rv += SDK_ASSERT(reg.superform->shortcuts().size() == 1);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Alt+S"))->action() == reg.pluginManager);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Q"))->action() == reg.views);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("R")) == NULL);
  // Go back to default; restore an action that doesn't exist (i.e. delete views)
  defaultSettings->restore(*reg.reg);
  rv += SDK_ASSERT(0 == reg.reg->removeAction("Views"));
  // Restore the configuration that had a views hotkey, make sure no crash
  newSettings->restore(*reg.reg);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Alt+S"))->action() == reg.pluginManager);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Q")) == NULL);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("R")) == NULL);
  delete newSettings;
  // Restore the original settings, after deleting the new ones
  defaultSettings->restore(*reg.reg);
  rv += SDK_ASSERT(reg.superform->shortcuts().size() == 2);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Alt+S"))->action() == reg.superform);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("Q")) == NULL);
  rv += SDK_ASSERT(reg.reg->findAction(QKeySequence("R")) == NULL);

  // Looks good, return
  delete defaultSettings;
  return rv;
}

int testAlias()
{
  int rv = 0;

  ActionRegistry* ar = new ActionRegistry(NULL);

  // Make an action
  QAction* action1 = new QAction("acttion1", NULL);
  simQt::Action* firstAction = ar->registerAction("Test", "First", action1);
  rv += SDK_ASSERT(firstAction != NULL);

  // Add an alias; which should work
  rv += SDK_ASSERT(ar->registerAlias("First", "FirstAlias1") == 0);
  // Add the same alias again which should fail
  rv += SDK_ASSERT(ar->registerAlias("First", "FirstAlias1") != 0);

  // Add a second alias; which should work
  rv += SDK_ASSERT(ar->registerAlias("First", "FirstAlias2") == 0);
  // Add the same alias again which should fail
  rv += SDK_ASSERT(ar->registerAlias("First", "FirstAlias1") != 0);

  // Execute via name and alias
  rv += SDK_ASSERT(ar->execute("First") == 0);
  rv += SDK_ASSERT(ar->execute("FirstAlias1") == 0);
  rv += SDK_ASSERT(ar->execute("FirstAlias2") == 0);

  // Execute a bogus name that should fail
  rv += SDK_ASSERT(ar->execute("ShouldNotWork") != 0);

  // Find via name and alias
  rv += SDK_ASSERT(ar->findAction("First") == firstAction);
  rv += SDK_ASSERT(ar->findAction("FirstAlias1") == firstAction);
  rv += SDK_ASSERT(ar->findAction("FirstAlias2") == firstAction);

  // Find a bogus name that should fail
  rv += SDK_ASSERT(ar->findAction("ShouldNotWork") == NULL);

  delete ar;
  delete action1;

  return rv;
}

}

int ActionRegistryTest(int argc, char* argv[])
{
  if (simVis::isHeadless())
  {
    std::cerr << "Headless display detected; aborting test." << std::endl;
    return 0;
  }
  int rv = 0;
  QApplication app(argc, argv);
  rv += testFind();
  rv += testSetHotKey();
  rv += testAddHotKey();
  rv += testExecute();
  rv += testMemento();
  rv += testAlias();
  return rv;
}

