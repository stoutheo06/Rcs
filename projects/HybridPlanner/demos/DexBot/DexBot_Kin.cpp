/*******************************************************************************

  Copyright (C) by
  Honda Research Institute Europe GmbH,
  Carl-Legien Str. 30
  63073 Offenbach/Main
  Germany

  UNPUBLISHED PROPRIETARY MATERIAL.
  ALL RIGHTS RESERVED

*******************************************************************************/


#include <Rcs_macros.h>
#include <Rcs_cmdLine.h>
#include <Rcs_resourcePath.h>
#include <Rcs_timer.h>
#include <Rcs_typedef.h>
#include <Rcs_parser.h>

#include <GraphNode.h>
#include <CapsuleNode.h>
#include <HUD.h>
#include <KeyCatcher.h>
#include <JointWidget.h>
#include <RcsViewer.h>
#include <Rcs_guiFactory.h>

#include <SegFaultHandler.h>

#include <IkSolverRMR.h>
#include <ControllerWidgetBase.h>

#include <Rcs_sensor.h>
#include <Rcs_math.h>
#include <COSNode.h>


#include <iostream>
#include <csignal>


RCS_INSTALL_ERRORHANDLERS


bool runLoop = true;



/*******************************************************************************
 * Ctrl-C destructor. Tries to quit gracefully with the first Ctrl-C
 * press, then just exits.
 ******************************************************************************/
void quit(int /*sig*/)
{
  static int kHit = 0;
  runLoop = false;
  fprintf(stderr, "Trying to exit gracefully - %dst attempt\n", kHit + 1);
  kHit++;

  if (kHit == 2)
  {
    fprintf(stderr, "Exiting without cleanup\n");
    exit(0);
  }
}


/*******************************************************************************
 * A few pre-defined models
 ******************************************************************************/
static bool getModel(char* directory, char* xmlFileName)
{
  Rcs::CmdLineParser argP;

  if (!argP.hasArgument("-model", "Example models: Husky, DexBot"))
  {
    return false;
  }

  char model[64] = "";
  argP.getArgument("-model", model);

  if (STREQ(model, "Husky"))
  {
    strcpy(directory, "config/xml/Husky");
    strcpy(xmlFileName, "husky-all.xml");
  }
  else if (STREQ(model, "DexBot"))
  {
    strcpy(directory, "config/xml/DexBot");
    strcpy(xmlFileName, "gScenario.xml");
  }
  else
  {
    RMSG("Unknown model: %s", model);
    strcpy(directory, "");
    strcpy(xmlFileName, "");
    return false;
  }

  return true;
}





/*******************************************************************************
 *
 ******************************************************************************/
int main(int argc, char** argv)
{
  RMSG("Starting Rcs...");

  const char* hgrDir = getenv("RCS_ROBOT_MODELS");
  if (hgrDir != NULL)
  {
    std::string meshDir = std::string(hgrDir);
    Rcs_addResourcePath(meshDir.c_str());
  }

  // Default parameters set
  int mode = 0;
  int simpleGraphics = 0;
  char xmlFileName[128] = "", directory[128] = "";

  // Ctrl-C callback handler
  signal(SIGINT, quit);

  // This initialize the xml library and check potential mismatches between
  // the version it was compiled for and the actual shared library used.
  LIBXML_TEST_VERSION;

  // Parse command line arguments
  Rcs::CmdLineParser argP(argc, argv);
  argP.getArgument("-dl", &RcsLogLevel, "Debug level (default is 0)");
  argP.getArgument("-m", &mode, "Test mode");
  argP.getArgument("-f", xmlFileName, "Configuration file name");
  argP.getArgument("-dir", directory, "Configuration file directory");
  bool valgrind = argP.hasArgument("-valgrind",
                                      "Start without Guis and graphics");
  simpleGraphics = argP.hasArgument("-simpleGraphics", "OpenGL without "
                                       "fancy stuff (shadows, anti-aliasing)");

  // Initialize GUI and OSG mutex
  pthread_mutex_t graphLock;
  pthread_mutex_init(&graphLock, NULL);

  // Option without mutex for viewer
  pthread_mutex_t* mtx = &graphLock;
  if (argP.hasArgument("-nomutex", "Graphics without mutex"))
  {
    mtx = NULL;
  }

  runLoop = true;

  Rcs_addResourcePath("config");

  RPAUSE_DL(5);

  switch (mode)
  {

    // ==============================================================
    // pending
    // ==============================================================
    case 0:
    {
			// .............................
			// Initialisation of required parameters
			// .............................
      bool calcDistance = true;
      double jlCost = 0.0, dJlCost = 0.0;


    	// for physics
      double dt = 0.005, tmc = 0.01, clipLimit = 0.1;
      double alpha = 0.05, lambda = 1.0e-8, dt_calc = 0.0;
      char hudText[2056] = "";


      // for scenario and robot
      strcpy(xmlFileName, "cAction_Kin.xml");
      strcpy(directory, "config/xml/DexBot");

    	// .............................
    	// arg parser
     	// .............................

      int motion = 1;

      // flag args
      bool pause = argP.hasArgument("-pause", "Hit key for each iteration");
      bool velCntrl = argP.hasArgument("-velCntrl",
                                          "Enforce position control");

      bool skipGui = argP.hasArgument("-skipGui",
                                         "No joint angle command Gui");
      bool manipulability = argP.hasArgument("-manipulability",
                                         "Manipulability criterion in "
                                         "null space");

      // value args
      argP.getArgument("-dt", &dt, "Simulation time step (default is %f)",
                          dt);
      argP.getArgument("-tmc", &tmc, "Gui filter, smaller is softer (default"
                          " is: %f)",
                          tmc);
      argP.getArgument("-f", xmlFileName,
                          "Configuration file name (default is \"%s\")",
                          xmlFileName);
      argP.getArgument("-dir", directory,
                          "Configuration file directory (default is \"%s\")",
                          directory);


      if (argP.hasArgument("-h"))
      {
        RMSG("Mode %d: Rcs.exe -m %d -dir <graph-directory> -f "
             "<graph-file>\n\n\t- Creates a graph from an xml file\n\t"
             "- Creates a viewer (if option -valgrind is not set)\n\t"
             "- Creates a StateGui (if option -valgrind is not set)\n\t"
             "- Runs the kinematics in a physics enabled loop\n\n\t"
             "The joints angles can be modified by the sliders\n",
             mode, mode);
        break;
      }

      // .............................
      // Initialisation of instances
      // .............................

      // linking to appropriate dir
      Rcs_addResourcePath(directory);

      // parsing the xml files
      getModel(directory, xmlFileName);

      // Create controller
      Rcs::ControllerBase controller(xmlFileName, true);
      Rcs::IkSolverRMR ikSolver(&controller);

        	// .............................
    	// keycather registration
     	// .............................
      Rcs::KeyCatcherBase::registerKey("q", "Quit");
      Rcs::KeyCatcherBase::registerKey("Space", "Toggle pause");
      Rcs::KeyCatcherBase::registerKey("S", "Print out joint torques");
      Rcs::KeyCatcherBase::registerKey("o", "Toggle distance calculation");
      Rcs::KeyCatcherBase::registerKey("m", "Manipulability null space");


      // set all joints to velocity control
      if (velCntrl == true)
      {
        RCSGRAPH_TRAVERSE_JOINTS(controller.getGraph())
        {
          if ((JNT->ctrlType == RCSJOINT_CTRL_VELOCITY) ||
              (JNT->ctrlType == RCSJOINT_CTRL_TORQUE))
          {
            JNT->ctrlType = RCSJOINT_CTRL_VELOCITY;
            // JNT->ctrlType = RCSJOINT_CTRL_POSITION;
          }
        }
        RcsGraph_setState(controller.getGraph(), NULL, NULL);
      }

      // for the reset of the initial state
      MatNd* q0 = MatNd_clone(controller.getGraph()->q);

      // joint position related quantities
      // replicates for control
      MatNd* q_des = MatNd_clone(controller.getGraph()->q);             // q positions
      MatNd* dq_des  = MatNd_create(controller.getGraph()->dof, 1);     // q deltas
      MatNd* q_dot_des  = MatNd_create(controller.getGraph()->dof, 1);  // q velocities

      // actuall q values of the robot
      MatNd* q_curr = MatNd_clone(controller.getGraph()->q);            // q positions
      MatNd* q_dot_curr = MatNd_create(controller.getGraph()->dof, 1);  // q velocities

      // variables related to IK
      MatNd* a_des   = MatNd_create(controller.getNumberOfTasks(), 1);  // activation vector
      MatNd* dH      = MatNd_create(1, controller.getGraph()->nJ);      // Gradient mat

      // task(cartesian) variables that belong to the GUI
      MatNd* x_des   = MatNd_create(controller.getTaskDim(), 1);        // task final position
      MatNd* x_goal = MatNd_create(controller.getTaskDim(), 1);         // task intermediate position
      MatNd* x_dot_goal = MatNd_create(controller.getTaskDim(), 1);     // task velocities
      MatNd* dx_des  = MatNd_create(controller.getTaskDim(), 1);        // task deltas

      // actuall task values of the robot
      MatNd* x_curr  = MatNd_create(controller.getTaskDim(), 1);

      // set task activation vector
      controller.readActivationsFromXML(a_des);
      // controller.readActivationVectorFromXML(a_des, "activation");
      // make the current activation vector
      controller.computeX(x_curr);
      MatNd_copy(x_curr, x_des);

      // timer and loopcounter
			Timer* timer = Timer_create(dt);
      unsigned int loopCount = 0;

			// Viewer and Gui
			Rcs::KeyCatcher* kc = NULL;
			Rcs::Viewer* viewer = NULL;
			Rcs::HUD* hud = NULL;
      Rcs::GraphNode* gn  = NULL;

			if (valgrind==false)
			{
			  // HUD
			  hud = new Rcs::HUD(0,0,500,160);

			  // keycather
			  kc = new Rcs::KeyCatcher();

        // viewer
        viewer = new Rcs::Viewer(!simpleGraphics, !simpleGraphics);
        gn = new Rcs::GraphNode(controller.getGraph());

        viewer->add(gn);

        viewer->add(hud);
        viewer->add(kc);

        viewer->runInThread(mtx);


			  if (skipGui==false)
			  {
          Rcs::ControllerWidgetBase::create(&controller, a_des, x_des, x_curr, mtx);
			  }
			}

      // New experimental code starts here.....
      RMSG("Size of x_des %d:", x_des->size);

      int taskIndex = controller.getTaskIndex("Box XYZ");
      size_t TaskDim = controller.getTaskDim(taskIndex);
      RMSG("The index of the task %i ,%i ", taskIndex, TaskDim);


      MatNd* x_Box_Traj = MatNd_create(1000, 1);
      MatNd_setElementsTo(x_Box_Traj, 0.001);
      for (unsigned int i=1; i<x_Box_Traj->m; i++)
      {
        MatNd_addToEle(x_Box_Traj, i, 0, MatNd_get(x_Box_Traj, i-1, 0) );
      }
      unsigned int counter = 0;
      double new_target = 0;
      // --------------------------------------------------------------------
			// Starting infinite loop ---------------------------------------------

			while (runLoop)
			{
        //................................
        // Actual control section
        //................................
        pthread_mutex_lock(&graphLock);

        dt_calc = Timer_getTime();

        /////////////////////////////////////////////////////////////////
        // Compute trajectory
        // Apply desired joint angles from Gui with interpolation
        // if the joints are position control
        /////////////////////////////////////////////////////////////////
        for (unsigned int i=3; i<x_des->m; i++)
        {
          x_goal->ele[i] = (1.0-tmc)*x_goal->ele[i] + tmc*x_des->ele[i];
        }

        if (counter < 1000)
        {

          x_goal->ele[2] = (1.0-tmc)*x_goal->ele[2] + tmc*(new_target + motion*MatNd_get(x_Box_Traj, counter, 0));
          counter += 1;
        }
        /////////////////////////////////////////////////////////////////
        // Compute control input
        /////////////////////////////////////////////////////////////////
        controller.computeDX(dx_des, x_goal);

        // clip dx
        MatNd clipArr = MatNd_fromPtr(1, 1, &clipLimit);
        MatNd_saturateSelf(dx_des, &clipArr);

        // computes the gradient of a cost function w.r.t the joint limits
        controller.computeJointlimitGradient(dH);

        // compute cost to collision
        if (calcDistance==true)
        {
          controller.computeCollisionCost();
        }

        // compute manipulability gradient
        if (manipulability)
        {
          MatNd_setZero(dH);
          controller.computeManipulabilityGradient(dH, a_des);
          MatNd_constMulSelf(dH, 100.0);
        }

        // mult with const
        MatNd_constMulSelf(dH, alpha);

        // solve IK right#
        ikSolver.solveRightInverse(dq_des, dx_des, dH, a_des, lambda);

        // differentiate in case we want to control velocities
        MatNd_constMul(q_dot_des, dq_des, 1.0/dt);

        // update q
        MatNd_addSelf(q_des, dq_des);
        // MatNd_addSelf(controller.getGraph()->q, dq_des);

        //////////////////////////////////////////////////////////////
        // Forward kinematics ( update graph )
        //////////////////////////////////////////////////////////////
        RcsGraph_copyRigidBodyDofs(q_des, controller.getGraph(), q_curr);
        RcsGraph_setState(controller.getGraph(), q_des, NULL);
        controller.computeX(x_curr);

        // update cost to joint limits
        dJlCost = -jlCost;
        jlCost = controller.computeJointlimitCost();
        dJlCost += jlCost;

        dt_calc = Timer_getTime() - dt_calc;

	      pthread_mutex_unlock(&graphLock);


         ////////////////////////////////////////////////////////////
        // Keycatcher
        /////////////////////////////////////////////////////////////////

        if (pause==true)
        {
          RPAUSE_MSG("Hit enter to continue iteration %u", loopCount);
        }

        if (kc && kc->getAndResetKey('q'))
        {
          RMSGS("Quitting run loop");
          runLoop = false;
        }
        else if (kc && kc->getAndResetKey('o'))
        {
          calcDistance = !calcDistance;
          RMSG("Distance calculation is %s", calcDistance ? "ON" : "OFF");
        }
        else if (kc && kc->getAndResetKey('m'))
        {
          manipulability = !manipulability;
          RMSG("Manipulation index nullspace is %s",
               manipulability ? "ON" : "OFF");
        }
        else if (kc && kc->getAndResetKey(' '))
        {
          pause = !pause;
          RMSG("Pause modus is %s", pause ? "ON" : "OFF");
        }
        else if (kc && kc->getAndResetKey('u'))
        {
          motion = -1*motion; // inverse motion
          counter = 0;       // restart counter
          new_target = x_goal->ele[2];
          RMSG("motion modus is %i", motion);
        }


        if (valgrind)
        {
          RLOG(1, "Step");
        }

	      //................................
	      if (valgrind)
        {
          RLOG(1, "Step");
        }

	      // write in the inviewer box
	      // sprintf(hudText, "[%s]: Sim-step: %.1f ms\nViewer fps: %.1f Hz",
        //         sim->getClassName(), dtSim*1000.0, 0.0/*viewer->fps*/);

        if (hud != NULL)
        {
          hud->setText(hudText);
        }
        else
        {
          std::cout << hudText;
        }

        Timer_waitNoCatchUp(timer);

        loopCount++;

        if (loopCount>10 && (valgrind==true))
        {
          runLoop = false;
        }

	    }

	    // cleaning up all pointer allocations
	    Timer_destroy(timer);
      if (!valgrind)
      {
        RcsGuiFactory_shutdown();
        delete viewer;
      }

      MatNd_destroy(q0);
      MatNd_destroy(q_des);
      MatNd_destroy(dq_des);
      MatNd_destroy(q_dot_des);
      MatNd_destroy(q_curr);
      MatNd_destroy(q_dot_curr);
      MatNd_destroy(a_des);
      MatNd_destroy(dH);
      MatNd_destroy(x_des);
      MatNd_destroy(x_goal);
      MatNd_destroy(x_dot_goal);
      MatNd_destroy(dx_des);
      MatNd_destroy(x_curr);

      break;
	  }

	default:
	{
	  RMSG("there is no mode %d", mode);
	}

	}

  if ((mode!=0) && argP.hasArgument("-h"))
  {
    argP.print();
    Rcs::KeyCatcherBase::printRegisteredKeys();
    Rcs_printResourcePath();
  }

  // Clean up global stuff. From the libxml2 documentation:
  // WARNING: if your application is multithreaded or has plugin support
  // calling this may crash the application if another thread or a plugin is
  // still using libxml2. It's sometimes very hard to guess if libxml2 is in
  // use in the application, some libraries or plugins may use it without
  // notice. In case of doubt abstain from calling this function or do it just
  // before calling exit() to avoid leak reports from valgrind !
  xmlCleanupParser();

  fprintf(stderr, "Thanks for using the Rcs libraries\n");

#if defined (_MSC_VER)
  if ((mode==0) || argP.hasArgument("-h"))
  {
    RPAUSE();
  }
#endif

  return 0;
}