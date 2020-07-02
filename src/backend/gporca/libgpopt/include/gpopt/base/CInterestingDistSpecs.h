//---------------------------------------------------------------------------
// Greenplum Database
// Copyright (c) 2020 VMware Inc.
//
// CInterestingDistSpecs.h
//
// Interesting distribution specs as part of the derived
// relational properties.
//---------------------------------------------------------------------------

#ifndef GPOPT_CInterestingDistSpecs_H
#define GPOPT_CInterestingDistSpecs_H

#include "gpos/base.h"
#include "gpos/common/CRefCount.h"
#include "gpopt/base/CDistributionSpec.h"
#include "gpopt/base/CDistributionSpecHashed.h"

namespace gpopt
{
	using namespace gpos;

	// fwd declaration
	class CExpressionHandle;
	class CColRefSet;
	class CReqdPropPlan;
	class CKeyCollection;
	class CPartIndexMap;
	class CPropConstraint;
	class CPartInfo;

	//---------------------------------------------------------------------------
	// CInterestingDistSpecs
	//
	// Interesting distribution specs are distribution specs that are likely to
	// be "native" distribution spec of this node once we have generated a plan.
	// "Native" means that the distribution spec will be achieved without a
	// motion at this level (there may be motions below this operator, though).
	// Right now, all interesting distribution specs are hash distributions.
	// Examples:
	//   - For a logical get, the interesting distribution spec is the
	//     distribution spec of the table (if applicable).
	//   - For nodes with no inherent distribution spec, such as a VALUES or
	//     a table function, there are no interesting distribution specs.
	//   - For a join, the interesting distribution specs are either those of
	//     its outer child (assuming the inner gets broadcast), or those
	//     interesting child distribution specs that have a subset of the
	//     equi-join columns as the distribution key.
	//
	//---------------------------------------------------------------------------
	class CInterestingDistSpecs : public CRefCount
	{
	private:

		// the info we collect for a single interesting distribution spec
		struct SInterestingDistribution
		{
			// the hash distribution spec itself
			CDistributionSpecHashed *m_dist_spec;

			// whether we must have an exact match (e.g. table) or whether
			// a "satisfy" match is ok (e.g. for a groupby or join with no
			// matching interesting child distribution specs)
			BOOL m_match_is_exact;

			// an estimate of the data size (rows * row width), to estimate the
			// cost of the motion we can save by using this "native" distribution
			CDouble m_data_size;

			SInterestingDistribution(
									 CDistributionSpecHashed *ds,
									 BOOL isExact = true
									) :
									m_dist_spec(ds),
									m_match_is_exact(isExact),
									m_data_size(-1.0)
			{}

			~SInterestingDistribution()
			{
				m_dist_spec->Release();
			}

			IOstream &OsPrint(IOstream &os) const
			{
				os << "Dist Spec: ";
				m_dist_spec->OsPrint(os);
				os << ", Exact: " << m_match_is_exact;
				os << ", Size: " << m_data_size;

				return os;
			}
		};

		typedef CDynamicPtrArray<SInterestingDistribution, CleanupDelete> CInterestingDistributionArray;

		// an array with interesting distribution specs, in no particular order
		CInterestingDistributionArray *m_dist_spec_array;

		// this gets set once we have added information about the size of
		// the data to be redistributed (after statistics have been derived)
		BOOL m_has_size_information;

	public:

		// ctor
		CInterestingDistSpecs(CMemoryPool *mp) : m_has_size_information(false)
		{
			m_dist_spec_array = GPOS_NEW(mp) CInterestingDistributionArray(mp);
		}

		// dtor
		virtual
		~CInterestingDistSpecs()
		{
			m_dist_spec_array->Release();
		}

		ULONG Size() const
		{
			return (m_dist_spec_array->Size());
		}

		CDistributionSpecHashed *GetDistributionSpec(ULONG ix)
		{
			return (*m_dist_spec_array)[ix]->m_dist_spec;
		}

		BOOL GetIsExact(ULONG ix)
		{
			return (*m_dist_spec_array)[ix]->m_match_is_exact;
		}

		CDouble GetDataSize(ULONG ix)
		{
			return (*m_dist_spec_array)[ix]->m_data_size;
		}

		BOOL HasSizeInformation()
		{
			return m_has_size_information;
		}

		void AddSizeInformation(CExpressionHandle &exprhdl, CDouble group_data_size)
		{
			// add the size information to any distribution specs that originate
			// in this node, use the children's size information for those that
			// come from the children
			for (ULONG d=0; d<m_dist_spec_array->Size(); d++)
			{
				CDouble data_size(-1.0);
				SInterestingDistribution *origin_spec = FindChildOriginSpec(GetDistributionSpec(d), exprhdl);

				if (NULL != origin_spec)
				{
					data_size = origin_spec->m_data_size;
				}
				else
				{
					data_size = group_data_size;
				}
				(*m_dist_spec_array)[d]->m_data_size = data_size;
			}
			m_has_size_information = true;
		}

		SInterestingDistribution *FindChildOriginSpec(CDistributionSpecHashed *, CExpressionHandle &)
		{
//			for (ULONG c=0; c<exprhdl.Arity(); c++)
//			{
//
//			}
			// TODO: implement this
			return NULL;
		}

		CDistributionSpecHashed *GetMostInterestingDistributionSpec(BOOL &is_exact)
		{
			CDistributionSpecHashed *result = NULL;
			CDouble best_size(0.0);

			is_exact = true;

			for (ULONG ix=0; ix>Size(); ix++)
			{
				if (NULL == result || GetDataSize(ix) > best_size)
				{
					// this is our best candidate so far, the one with the biggest size
					// (and therefore biggest effort to redistribute)
					result = GetDistributionSpec(ix);
					is_exact = GetIsExact(ix);
				}
			}

			return result;
		}

		void Add(CMemoryPool *mp, CDistributionSpec *ds, BOOL isExact = true)
		{
			CDistributionSpecHashed *ds_hash = CDistributionSpecHashed::PdsConvert(ds);

			if (NULL != ds_hash)
			{
				m_dist_spec_array->Append(GPOS_NEW(mp) SInterestingDistribution(ds_hash, isExact));
			}
		}

		void Add(CMemoryPool *mp, CInterestingDistSpecs *ids)
		{
			if (NULL != ids)
			{
				for (ULONG ix=0; ix<ids->Size(); ix++)
				{
					CDistributionSpecHashed *pdsh = ids->GetDistributionSpec(ix);

					pdsh->AddRef();
					Add(mp, pdsh, ids->GetIsExact(ix));
				}
			}
		}

		// print
		virtual
		IOstream &OsPrint(IOstream &os) const
		{
			os << "[";
			for (ULONG d=0; d<Size(); d++)
			{
				if (0 < d)
				{
					os << ", ";
				}
				(*m_dist_spec_array)[d]->OsPrint(os);
			}
			os << "]";
			return os;
		}

	}; // class CInterestingDistSpecs

	// shorthand for printing
	inline
	IOstream &operator << (IOstream &os, CInterestingDistSpecs &ids)
	{
		return ids.OsPrint(os);
	}

}


#endif /* GPOPT_CInterestingDistSpecs_H */
