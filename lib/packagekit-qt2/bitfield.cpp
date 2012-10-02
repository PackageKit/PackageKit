#include "bitfield.h"

using namespace PackageKit;

Bitfield::Bitfield () : m_val (0)
{
}

Bitfield::Bitfield (qulonglong val) : m_val (val)
{
}

Bitfield::~Bitfield ()
{
}

qulonglong Bitfield::operator& (qulonglong mask) const
{
	return m_val & (1ULL << mask);
}

qulonglong Bitfield::operator&= (qulonglong mask)
{
	m_val &= (1ULL << mask);
	return m_val;
}

qulonglong Bitfield::operator| (qulonglong mask) const
{
	return m_val | (1ULL << mask);
}

qulonglong Bitfield::operator|= (qulonglong mask)
{
	m_val |= (1ULL << mask);
	return m_val;
}

Bitfield Bitfield::operator& (Bitfield mask) const
{
	return m_val & mask.m_val;
}

Bitfield Bitfield::operator&= (Bitfield mask)
{
	m_val &= mask.m_val;
	return m_val;
}

Bitfield Bitfield::operator| (Bitfield mask) const
{
	return m_val | mask.m_val;
}

Bitfield Bitfield::operator|= (Bitfield mask)
{
	m_val |= mask.m_val;
	return m_val;
}


Bitfield& Bitfield::operator= (const Bitfield& other)
{
	if (this == &other)
		return *this;

	m_val = other.m_val;

	return *this;
}
