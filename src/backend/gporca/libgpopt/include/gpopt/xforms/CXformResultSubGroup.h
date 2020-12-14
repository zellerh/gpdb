//---------------------------------------------------------------------------
// Greenplum Database
// Copyright (c) 2020 VMware and affiliates, Inc.
//
// Generate multiple xform alternatives that share common subgroups.
//
// This happens mainly with NAry joins: Let's say we transform a
// 4-way join of tables { a, b, c, d } and the two generated alternatives are
// ((a join b) join c) join d
// ((b join c) join a) join d
// Note that both alternatives have a subtree that is a join of { a, b, c }.
// We want those to share a common group (class CGroup), once we insert
// them into Memo.
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformResultSubGroup_H
#define GPOPT_CXformResultSubGroup_H

#include "gpos/common/CRefCount.h"

namespace gpopt
{
class CGroup;

using namespace gpos;

class CXformResultSubGroup : public CRefCount
{
private:
	CGroup *m_group;

public:
	CXformResultSubGroup() : m_group(NULL)
	{
	}

	CGroup *
	getGroup()
	{
		return m_group;
	}

	void
	SetGroup(CGroup *group)
	{
		m_group = group;
	}
};

}  // namespace gpopt

#endif	// !GPOPT_CXformResultSubGroups_H

// EOF
