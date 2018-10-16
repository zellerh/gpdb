//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CJoinOrderMinCard.cpp
//
//	@doc:
//		Implementation of cardinality-based join order generation
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpos/io/COstreamString.h"
#include "gpos/string/CWStringDynamic.h"

#include "gpos/common/CAutoRef.h"
#include "gpos/common/clibwrapper.h"
#include "gpos/common/CBitSet.h"

#include "gpopt/base/CDrvdPropScalar.h"
#include "gpopt/base/CColRefSetIter.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/operators/ops.h"
#include "gpopt/operators/CPredicateUtils.h"
#include "gpopt/operators/CNormalizer.h"
#include "gpopt/xforms/CJoinOrderMinCard.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CJoinOrderMinCard::CJoinOrderMinCard
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CJoinOrderMinCard::CJoinOrderMinCard
	(
	IMemoryPool *mp,
	CExpressionArray *pdrgpexprComponents,
	CExpressionArray *pdrgpexprConjuncts
	)
	:
	CJoinOrder(mp, pdrgpexprComponents, pdrgpexprConjuncts, true),
	m_pcompResult(NULL)
{
#ifdef GPOS_DEBUG
	for (ULONG ul = 0; ul < m_ulComps; ul++)
	{
		GPOS_ASSERT(NULL != m_rgpcomp[ul]->m_pexpr->Pstats() &&
				"stats were not derived on input component");
	}
#endif // GPOS_DEBUG
}


//---------------------------------------------------------------------------
//	@function:
//		CJoinOrderMinCard::~CJoinOrderMinCard
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CJoinOrderMinCard::~CJoinOrderMinCard()
{
	CRefCount::SafeRelease(m_pcompResult);
}


//---------------------------------------------------------------------------
//	@function:
//		CJoinOrderMinCard::MarkUsedEdges
//
//	@doc:
//		Mark edges used by result component
//
//---------------------------------------------------------------------------
void
CJoinOrderMinCard::MarkUsedEdges()
{
	GPOS_ASSERT(NULL != m_pcompResult);

	CExpression *pexpr = m_pcompResult->m_pexpr;
	COperator::EOperatorId op_id = pexpr->Pop()->Eopid();
	if (0 == pexpr->Arity() ||
		(COperator::EopLogicalSelect != op_id &&
		 COperator::EopLogicalInnerJoin != op_id &&
		 COperator::EopLogicalLeftOuterJoin != op_id))
	{
		// result component does not have a scalar child, e.g. a Get node
		return;
	}

	CExpression *pexprScalar = (*pexpr) [pexpr->Arity() - 1];
	CExpressionArray *pdrgpexprScalar = CPredicateUtils::PdrgpexprConjuncts(m_mp, pexprScalar);
	const ULONG ulSizeScalar = pdrgpexprScalar->Size();

	// Find the correct edge to mark as used.  All the conjucts of the edge expr
	// must match some conjuct of the scalar expr of m_compResults for that edge
	// to be marked as used. This way edges that contain multiple conjucts are
	// also matched correctly.
	for (ULONG ulEdge = 0; ulEdge < m_ulEdges; ulEdge++)
	{
		SEdge *pedge = m_rgpedge[ulEdge];
		if (pedge->m_fUsed)
		{
			continue;
		}

		CExpressionArray *pdrgpexprEdge = CPredicateUtils::PdrgpexprConjuncts(m_mp, pedge->m_pexpr);
		const ULONG ulSizeEdge = pdrgpexprEdge->Size();

#ifdef GPOS_DEBUG
		CAutoRef<CBitSet> pbsScalarConjuctsMatched(GPOS_NEW(m_mp) CBitSet(m_mp));
#endif
		ULONG ulMatchCount = 0; // Count of edge predicate conjucts matched
		// For each conjuct of the edge predicate
		for (ULONG ulEdgePred = 0; ulEdgePred < ulSizeEdge; ++ulEdgePred)
		{
			// For each conjuct of the scalar predicate
			for (ULONG ulScalarPred = 0; ulScalarPred < ulSizeScalar; ulScalarPred++)
			{
				if ((*pdrgpexprScalar)[ulScalarPred] == (*pdrgpexprEdge)[ulEdgePred])
				{
					// Count the number of edge predicate conjucts matched
					ulMatchCount++;
#ifdef GPOS_DEBUG
					// Make sure each match is unique ie. each scalar conjuct matches
					// only one edge conjunct
					GPOS_ASSERT(!pbsScalarConjuctsMatched->Get(ulScalarPred));
					pbsScalarConjuctsMatched->ExchangeSet(ulScalarPred);
#endif
					break;
				}
			}
		}

		if (ulMatchCount == ulSizeEdge)
		{
			// All the predicates of the edge was matched -> Mark it as used.
			pedge->m_fUsed = true;
		}
		pdrgpexprEdge->Release();
	}
	pdrgpexprScalar->Release();
}

//---------------------------------------------------------------------------
//	@function:
//		CJoinOrderMinCard::PexprExpand
//
//	@doc:
//		Create join order
//
//---------------------------------------------------------------------------
CExpression *
CJoinOrderMinCard::PexprExpand()
{
	GPOS_ASSERT(NULL == m_pcompResult && "join order is already expanded");

	m_pcompResult = GPOS_NEW(m_mp) SComponent(m_mp, NULL /*pexpr*/);
	ULONG ulCoveredComps = 0;
	while (ulCoveredComps < m_ulComps)
	{
		CDouble dMinRows(0.0);
		SComponent *pcompBest = NULL; // best component to be added to current result
		SComponent *pcompBestResult = NULL; // result after adding best component

		for (ULONG ul = 0; ul < m_ulComps; ul++)
		{
			SComponent *pcompCurrent = m_rgpcomp[ul];
			if (pcompCurrent->m_fUsed)
			{
				// used components are already included in current result
				continue;
			}

			if (!IsValidOuterJoinCombination(m_pcompResult, pcompCurrent))
			{
				continue;
			}

			// combine component with current result and derive stats
			CJoinOrder::SComponent *pcompTemp;

			pcompTemp = PcompCombine(m_pcompResult, pcompCurrent);

			DeriveStats(pcompTemp->m_pexpr);
			CDouble rows = pcompTemp->m_pexpr->Pstats()->Rows();

			if (NULL == pcompBestResult || rows < dMinRows)
			{
				pcompBest = pcompCurrent;
				dMinRows = rows;
				pcompTemp->AddRef();
				CRefCount::SafeRelease(pcompBestResult);
				pcompBestResult = pcompTemp;
			}
			pcompTemp->Release();
		}
		GPOS_ASSERT(NULL != pcompBestResult);

		// mark best component as used
		pcompBest->m_fUsed = true;
		m_pcompResult->Release();
		m_pcompResult = pcompBestResult;

		// mark used edges to avoid including them multiple times
		MarkUsedEdges();
		ulCoveredComps++;
	}
	GPOS_ASSERT(NULL != m_pcompResult->m_pexpr);

	CExpression *pexprResult = m_pcompResult->m_pexpr;
	pexprResult->AddRef();

	return pexprResult;
}


//---------------------------------------------------------------------------
//	@function:
//		CJoinOrderMinCard::OsPrint
//
//	@doc:
//		Print created join order
//
//---------------------------------------------------------------------------
IOstream &
CJoinOrderMinCard::OsPrint
	(
	IOstream &os
	)
	const
{
	if (NULL != m_pcompResult->m_pexpr)
	{
		os << *m_pcompResult->m_pexpr;
	}

	return os;
}

// EOF
