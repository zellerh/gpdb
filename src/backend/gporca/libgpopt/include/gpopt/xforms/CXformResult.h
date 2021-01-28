//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CXformResult.h
//
//	@doc:
//		Result container for all transformations
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformResult_H
#define GPOPT_CXformResult_H

#include "gpos/base.h"

#include "gpopt/base/CUtils.h"
#include "gpopt/operators/CExpression.h"
#include "gpopt/xforms/CXformResultSubGroup.h"

namespace gpopt
{
using namespace gpos;

typedef CHashMap<class CExpression, class CXformResultSubGroup,
				 CExpression::HashValue, CUtils::Equals,
				 CleanupRelease<CExpression>,
				 CleanupRelease<CXformResultSubGroup> >
	CCommonSubgroups;

//---------------------------------------------------------------------------
//	@class:
//		CXformResult
//
//	@doc:
//		result container
//
//---------------------------------------------------------------------------
class CXformResult : public CRefCount
{
private:
	// set of alternatives
	CExpressionArray *m_pdrgpexpr;

	// cursor for retrieval
	ULONG m_ulExpr;

	CMemoryPool *m_mp;

	// optional subgroups
	CCommonSubgroups *m_commonSubgroups;

public:
	CXformResult(const CXformResult &) = delete;

	// ctor
	explicit CXformResult(CMemoryPool *);

	// dtor
	~CXformResult() override;

	// accessor
	inline CExpressionArray *
	Pdrgpexpr() const
	{
		return m_pdrgpexpr;
	}

	// add alternative
	void Add(CExpression *pexpr);

	// retrieve next alternative
	CExpression *PexprNext();

	// assign an expression in an added alternative to a common subgroup
	void AssignCommonSubgroup(CExpression *subexpr,
							  CXformResultSubGroup *subgroup);

	// check whether an expression belongs to a common subgroup
	CXformResultSubGroup *GetCommonSubgroup(CExpression *subexpr);

	// print function
	IOstream &OsPrint(IOstream &os) const override;

};	// class CXformResult

// shorthand for printing
inline IOstream &
operator<<(IOstream &os, CXformResult &xfres)
{
	return xfres.OsPrint(os);
}

}  // namespace gpopt


#endif	// !GPOPT_CXformResult_H

// EOF
