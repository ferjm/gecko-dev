/* -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; -*- */
/*
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#include "prmon.h"
#include "nsCOMPtr.h"
#include "nsAppShell.h"
#include "nsIAppShell.h"
#include "nsIServiceManager.h"
#include "nsIEventQueueService.h"
#include "nsICmdLineService.h"
#include "nsIObserverService.h"
#include <stdlib.h>

#include "nsIWidget.h"
#include "nsIPref.h"

#include <Pt.h>
#include <errno.h>

PRBool            nsAppShell::mPtInited = PR_FALSE;
PRBool            ExitMainLoop;

/* This is defined to make the EventQueueToken look just like GTK's */
typedef int gint;

#include <prlog.h>
PRLogModuleInfo  *PhWidLog =  PR_NewLogModule("PhWidLog");
#include "nsPhWidgetLog.h"

//-------------------------------------------------------------------------
//
// XPCOM CIDs
//
//-------------------------------------------------------------------------
static NS_DEFINE_CID(kEventQueueServiceCID, NS_EVENTQUEUESERVICE_CID);
static NS_DEFINE_CID(kCmdLineServiceCID, NS_COMMANDLINE_SERVICE_CID);
static NS_DEFINE_CID(kPrefServiceCID, NS_PREF_CID);

// copied from nsEventQueue.cpp
static char *gEQActivatedNotification = "nsIEventQueueActivated";
static char *gEQDestroyedNotification = "nsIEventQueueDestroyed";

// a linked, ordered list of event queues and their tokens
class EventQueueToken {
public:
  EventQueueToken(const nsIEventQueue *aQueue, const gint aToken);

  const nsIEventQueue *mQueue;
  gint mToken;
  EventQueueToken *next;
};

EventQueueToken::EventQueueToken(const nsIEventQueue *aQueue, const gint aToken)
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("EventQueueToken::EventQueueToken Constructor aQueue=<%p> aToken=<%d>\n", aQueue, aToken));

  mQueue = aQueue;
  mToken = aToken;
  next = 0;
}

class EventQueueTokenQueue {
public:
  EventQueueTokenQueue();
  virtual ~EventQueueTokenQueue();
  void PushToken(nsIEventQueue *aQueue, gint aToken);
  PRBool PopToken(nsIEventQueue *aQueue, gint *aToken);

private:
  EventQueueToken *mHead;
};

EventQueueTokenQueue::EventQueueTokenQueue()
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("EventQueueTokenQueue::EventQueueTokenQueue Constructor\n"));
  mHead = 0;
}

EventQueueTokenQueue::~EventQueueTokenQueue()
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("EventQueueTokenQueue::~EventQueueTokenQueue Destructor\n"));

  NS_ASSERTION(!mHead, "event queue token deleted when not empty");
  // and leak. it's an error, anyway
}

void EventQueueTokenQueue::PushToken(nsIEventQueue *aQueue, gint aToken)
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("EventQueueTokenQueue::PushToken\n"));

  EventQueueToken *newToken = new EventQueueToken(aQueue, aToken);
  NS_ASSERTION(newToken, "couldn't allocate token queue element");
  if (!newToken)
    return NS_ERROR_OUT_OF_MEMORY;

  newToken->next = mHead;
  mHead = newToken;
  return NS_OK;
}

PRBool EventQueueTokenQueue::PopToken(nsIEventQueue *aQueue, gint *aToken)
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("EventQueueTokenQueue::PopToken\n"));

  EventQueueToken *token, *lastToken;
  PRBool          found = PR_FALSE;

  NS_ASSERTION(mHead, "attempt to retrieve event queue token from empty queue");
  if (mHead)
    NS_ASSERTION(mHead->mQueue == aQueue, "retrieving event queue from past head of queue queue");

  token = mHead;
  lastToken = 0;
  while (token && token->mQueue != aQueue) {
    lastToken = token;
    token = token->next;
  }
  if (token) {
    if (lastToken)
      lastToken->next = token->next;
    else
      mHead = token->next;
    found = PR_TRUE;
    *aToken = token->mToken;
    delete token;
  }
  return found;
}

//-------------------------------------------------------------------------
//
// nsAppShell constructor
//
//-------------------------------------------------------------------------
nsAppShell::nsAppShell()  
{ 
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::nsAppShell Constructor\n"));

  NS_INIT_REFCNT();
  mDispatchListener = nsnull;
//  mLock = PR_NewLock();
  mEventQueueTokens = new EventQueueTokenQueue();
//  mEventBufferSz = sizeof( PhEvent_t ) + 1000;
//  mEvent = (PhEvent_t*) PR_Malloc( mEventBufferSz );

  // throw on error would really be civilized here
//  NS_ASSERTION(mLock, "couldn't obtain lock in appshell");
  NS_ASSERTION(mEventQueueTokens, "couldn't allocate event queue token queue");
//  NS_ASSERTION( mEvent, "Out of memory" );

  RegisterObserver(PR_TRUE);

  /* Run this only once per application startup */
  if( !mPtInited )
  {
    PtInit( NULL );
    PtChannelCreate(); // Force use of pulses
    mPtInited = PR_TRUE;
  }
}

//-------------------------------------------------------------------------
//
// nsAppShell destructor
//
//-------------------------------------------------------------------------
nsAppShell::~nsAppShell()
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::~nsAppShell\n"));

//  PR_DestroyLock(mLock);
  delete mEventQueueTokens;
  
//  if (mEvent)
//    PR_Free(mEvent);

  RegisterObserver(PR_FALSE);
}


//-------------------------------------------------------------------------
//
// nsISupports implementation macro
//
//-------------------------------------------------------------------------
NS_IMPL_ISUPPORTS2(nsAppShell, nsIAppShell, nsIObserver)

//-------------------------------------------------------------------------
NS_IMETHODIMP nsAppShell::SetDispatchListener(nsDispatchListener* aDispatchListener)
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::SetDispatchListener.\n"));

  mDispatchListener = aDispatchListener;
  return NS_OK;
}

//-------------------------------------------------------------------------
//
// Enter a message handler loop
//
//-------------------------------------------------------------------------
int event_processor_callback(int fd, void *data, unsigned mode)
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::event_processor_callback fd=<%d> data=<%p> mode=<%d>\n", fd, data, mode));

  nsIEventQueue * eventQueue = (nsIEventQueue *) data;
  eventQueue->ProcessPendingEvents();
  return Pt_CONTINUE;
}


//-------------------------------------------------------------------------
//
// Create the application shell
//
//-------------------------------------------------------------------------

NS_IMETHODIMP nsAppShell::Create(int *bac, char **bav)
{
  char *home=nsnull;
  char *path=nsnull;

  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::Create\n"));

  int argc = bac ? *bac : 0;
  char **argv = bav;

  nsresult rv;

  NS_WITH_SERVICE(nsICmdLineService, cmdLineArgs, kCmdLineServiceCID, &rv);
  if (NS_SUCCEEDED(rv))
  {
    rv = cmdLineArgs->GetArgc(&argc);
    if(NS_FAILED(rv))
      argc = bac ? *bac : 0;

    rv = cmdLineArgs->GetArgv(&argv);
    if(NS_FAILED(rv))
      argv = bav;
  }

  return NS_OK;
}

//-------------------------------------------------------------------------
//
// Spinup - do any preparation necessary for running a message loop
//
//-------------------------------------------------------------------------
NS_METHOD nsAppShell::Spinup()
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::Spinup - Not Implemented.\n"));
  return NS_OK;
}

//-------------------------------------------------------------------------
//
// Spindown - do any cleanup necessary for finishing a message loop
//
//-------------------------------------------------------------------------
NS_METHOD nsAppShell::Spindown()
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::Spindown - Not Implemented.\n"));
  return NS_OK;
}


void MyMainLoop( void ) 
{
	ExitMainLoop = PR_FALSE;
	while (! ExitMainLoop)
	{
		PtProcessEvent();
	}
}

//-------------------------------------------------------------------------
//
// Run
//
//-------------------------------------------------------------------------
NS_IMETHODIMP nsAppShell::Run()
{
  NS_ADDREF_THIS();
  nsresult   rv = NS_OK;
  nsIEventQueue * EQueue = nsnull;

  // Get the event queue service 
  NS_WITH_SERVICE(nsIEventQueueService, eventQService, kEventQueueServiceCID, &rv);
  if (NS_FAILED(rv)) {
    NS_ASSERTION("Could not obtain event queue service", PR_FALSE);
    return rv;
  }

#ifdef DEBUG
  printf("Got the event queue from the service\n");
#endif /* DEBUG */

  //Get the event queue for the thread.
  rv = eventQService->GetThreadEventQueue(PR_GetCurrentThread(), &EQueue);

  // If a queue already present use it.
  if (EQueue)
    goto done;

  // Create the event queue for the thread
  rv = eventQService->CreateThreadEventQueue();
  if (NS_OK != rv) {
    NS_ASSERTION("Could not create the thread event queue", PR_FALSE);
    return rv;
  }
  //Get the event queue for the thread
  rv = eventQService->GetThreadEventQueue(PR_GetCurrentThread(), &EQueue);
  if (NS_OK != rv) {
    NS_ASSERTION("Could not obtain the thread event queue", PR_FALSE);
    return rv;
  }    

done:

#ifdef DEBUG
  printf("Calling PtAppAddFd with event queue\n");
#endif /* DEBUG */

    int err;
	err = PtAppAddFd(NULL,EQueue->GetEventQueueSelectFD(),Pt_FD_READ,event_processor_callback,EQueue);
    if (err == -1)
	{
		printf("nsAppShell::Run Error calling PtAppAddFd\n");
		exit(1);
	}

	MyMainLoop();		/* PtMainLoop() */

#ifdef DEBUG
  printf("Calling PtAppRemoveFd with event queue\n");
#endif /* DEBUG */

  err=PtAppRemoveFd(NULL,EQueue->GetEventQueueSelectFD());
  if (err==-1)
  {
	printf ("nsAppShell:: Run Error calling PtAppRemoveFd\n");
  }
  
  NS_IF_RELEASE(EQueue);
  Release();
  return NS_OK;
}

//-------------------------------------------------------------------------
//
// Exit a message handler loop
//
//-------------------------------------------------------------------------

NS_METHOD nsAppShell::Exit()
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::Exit.\n"));
  ExitMainLoop = PR_TRUE;
  return NS_OK;
}


NS_METHOD nsAppShell::GetNativeEvent(PRBool &aRealEvent, void *&aEvent)
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::GetNativeEvent\n"));

  nsresult res = NS_ERROR_FAILURE;
      
  aEvent = NULL;
  aRealEvent = PR_FALSE;

#if 0
  const int EVENT_SIZE = sizeof(struct PhEvent_t) + 1000;
  PhEvent_t *event = NULL;
  event = PR_Malloc(EVENT_SIZE);
  if (NULL != event)
  {
    int status;
	status = PhEventPeek(event, EVENT_SIZE);
	if (status == Ph_EVENT_MSG)
	{
  	  aRealEvent = PR_TRUE;
      aEvent = event;
	  res = NS_OK;
	}
    else
    {
	  PtProcessEvent();
      res = NS_OK;
    }
  }
  else
  {
    PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::GetNativeEvent out of memory\n"));
    NS_ASSERTION(event, "couldn't allocate a Photon event");

  }
#else
  PtProcessEvent();
  res = NS_OK;
#endif

  return res;
}


NS_METHOD nsAppShell::DispatchNativeEvent(PRBool aRealEvent, void * aEvent)
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::DispatchNativeEvent aRealEvent=<%d> aEvent=<%p>\n", aRealEvent, aEvent));

  int err;
  
  if ( aRealEvent == PR_TRUE )
  {
    err = PtEventHandler( (PhEvent_t*) aEvent );
    if (err == -1)
     {
        PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::DispatchNativeEvent aEvent=<%p> error=<%d>\n", aEvent, err));
        return NS_ERROR_FAILURE;
     }
  }

  return NS_OK;
}

NS_METHOD nsAppShell::EventIsForModalWindow(PRBool aRealEvent, void *aEvent, nsIWidget *aWidget, PRBool *aForWindow)
{
  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::EventIsForModalWindow aEvent=<%p> - Not Implemented.\n", aEvent));

#if 1
    *aForWindow = PR_FALSE;
    return NS_OK;
#else
  PRBool isInWindow, isMouseEvent;
  PhEvent_t *msg = (PhEvent_t *) aEvent;

  if (aRealEvent == PR_FALSE)
  {
    *aForWindow = PR_FALSE;
    return NS_OK;
  }

  isInWindow = PR_FALSE;
  if (aWidget != nsnull)
  {
    // Get Native Window for dialog window
    PtWidget_t *win;
    win = (PtWidget_t *)aWidget->GetNativeData(NS_NATIVE_WINDOW);
    PtWidget_t *eWin = (PtWidget_t *) msg->collector.handle;
    PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::EventIsForModalWindow win=<%p> eWin=<%p>\n",win, eWin));

    if (nsnull != eWin) {
      if (win == eWin) {
        isInWindow = PR_TRUE;
      }
    }
  }
  
  switch(msg->type)
  {
    case Ph_EV_BUT_PRESS:
    case Ph_EV_BUT_RELEASE:
    case Ph_EV_BUT_REPEAT:
    case Ph_EV_PTR_MOTION_BUTTON:
    case Ph_EV_PTR_MOTION_NOBUTTON:
       isMouseEvent = PR_TRUE;
       break;
    default:
       isMouseEvent = PR_FALSE;
       break;
  }

  *aForWindow = isInWindow == PR_TRUE || isMouseEvent == PR_FALSE ? PR_TRUE : PR_FALSE;

  PR_LOG(PhWidLog, PR_LOG_DEBUG, ("nsAppShell::EventIsForModalWindow isInWindow=<%d> isMouseEvent=<%d> aForWindow=<%d>\n", isInWindow, isMouseEvent, *aForWindow));
  return NS_OK;
#endif
}


//-------------------------------------------------------------------------
// nsIObserver interface
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
//
// Observe
//
//-------------------------------------------------------------------------

NS_IMETHODIMP nsAppShell::Observe(nsISupports *aSubject,
                                  const PRUnichar *aTopic,
                                  const PRUnichar *)
{
  // tell gdk to listen to the event queue or not. what happens
  // if multiple appshells (horrors, but it happens) repeat the same
  // instruction, one wonders.

  gint         queueToken;
  int err;

  nsAutoString topic(aTopic);

  if (topic.Equals(gEQActivatedNotification)) {
    nsCOMPtr<nsIEventQueue> eq(do_QueryInterface(aSubject));
    if (eq) {
	  err = PtAppAddFd(NULL,eq->GetEventQueueSelectFD(),Pt_FD_READ,event_processor_callback,eq);
      if (err == -1)
	  {  
		printf("nsAppShell::Observe Error calling PtAppAddFd errno=<%d>\n", errno);
        //exit(1);
	  }
      mEventQueueTokens->PushToken(eq, queueToken);
    }
  } else if (topic.Equals(gEQDestroyedNotification)) {
    nsCOMPtr<nsIEventQueue> eq(do_QueryInterface(aSubject));
    if (eq) {
      if (mEventQueueTokens->PopToken(eq, &queueToken)) {
        err=PtAppRemoveFd(NULL,eq->GetEventQueueSelectFD());
		if ( err == -1)
		{
          printf("nsAppShell::Observe Error calling PtAppRemoveFd errno=<%d>\n", errno);
		  //exit(1);
		}
      }
    }
  }
  return NS_OK;
}

//-------------------------------------------------------------------------
//
// Observe
//
//-------------------------------------------------------------------------
void nsAppShell::RegisterObserver(PRBool aRegister)
{
  nsresult rv;
  nsAutoString topicA(gEQActivatedNotification);
  nsAutoString topicB(gEQDestroyedNotification);

  NS_WITH_SERVICE(nsIObserverService, os, NS_OBSERVERSERVICE_PROGID, &rv);
  if (NS_SUCCEEDED(rv)) {
#if 0
    nsCOMPtr<nsIObserver> us(do_QueryInterface(this));
    if (us) {
#else
    nsIObserver *us;
    if (NS_SUCCEEDED(QueryInterface(nsIObserver::GetIID(), (void **) &us)) && us) {
#endif
      if (aRegister) {
        os->AddObserver(us, topicA.GetUnicode());
        os->AddObserver(us, topicB.GetUnicode());
      } else {
        os->RemoveObserver(us, topicA.GetUnicode());
        os->RemoveObserver(us, topicB.GetUnicode());
      }
      NS_RELEASE(us);
    }
  }
}

