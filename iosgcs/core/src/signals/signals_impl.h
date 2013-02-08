/*
**  ClanLib SDK
**  Copyright (c) 1997-2011 The ClanLib Team
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
**  Note: Some of the libraries ClanLib may link to may have additional
**  requirements or restrictions.
**
**  File Author(s):
**
**    Magnus Norddahl
*/
/*
 * ( c ) 2010-2013  Original developer
 * ( c ) 2013 The OpenPilot
 */

#pragma once


#include "../System/sharedptr.h"
#include <vector>

/// (Internal ClanLib Class)
/// \xmlonly !group=Core/Signals! !header=core.h! !hide! \endxmlonly
class CL_SlotCallback
{
public:
	CL_SlotCallback() : valid(true), enabled(true) { return; }

	virtual ~CL_SlotCallback() { return; }

	bool valid;

	bool enabled;
};

/// (Internal ClanLib Class)
/// \xmlonly !group=Core/Signals! !header=core.h! !hide! \endxmlonly
class CL_Slot_Impl
{
public:
	~CL_Slot_Impl() { if (callback) callback->valid = false; }

	CL_SharedPtr<CL_SlotCallback> callback;
};

/// (Internal ClanLib Class)
/// \xmlonly !group=Core/Signals! !header=core.h! !hide! \endxmlonly
class CL_Signal_Impl
{
public:
	std::vector< CL_SharedPtr<CL_SlotCallback> > connected_slots;
};


class CL_SignalQueue_Impl
{
public:
	virtual void invoke() = 0;
};

