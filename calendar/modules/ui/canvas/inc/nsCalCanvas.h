/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
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

#ifndef nsCalCanvas_h___
#define nsCalCanvas_h___

#include "nsXPFCCanvas.h"
#include "nsICalendarUser.h"

class nsCalCanvas : public nsXPFCCanvas

{
public:
  nsCalCanvas(nsISupports* outer);

  NS_DECL_ISUPPORTS

  NS_IMETHOD Init();

  NS_IMETHOD GetUser(nsICalendarUser *& aUser);  
  NS_IMETHOD SetUser(nsICalendarUser * aUser);

  NS_IMETHOD_(nsIModel *)   GetModel();
  NS_IMETHOD                GetModelInterface(const nsIID &aModelIID, nsISupports * aInterface) ;
  NS_IMETHOD                SetModel(nsIModel * aModel);

protected:
  ~nsCalCanvas();

protected:
  nsICalendarUser * mUser;

};

#endif /* nsCalCanvas_h___ */
