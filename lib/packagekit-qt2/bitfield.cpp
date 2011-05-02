#include "bitfield.h"

using namespace PackageKit;

Bitfield::Bitfield () : m_val (0)
{
}

Bitfield::Bitfield (qint64 val) : m_val (val)
{
}

Bitfield::~Bitfield ()
{
}

qint64 Bitfield::operator& (qint64 mask) const
{
	return m_val & (1 << mask);
}

qint64 Bitfield::operator&= (qint64 mask)
{
	m_val &= (1 << mask);
	return m_val;
}

qint64 Bitfield::operator| (qint64 mask) const
{
	return m_val | (1 << mask);
}

qint64 Bitfield::operator|= (qint64 mask)
{
	m_val |= (1 << mask);
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
