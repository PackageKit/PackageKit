#ifndef PACKAGEKIT_BITFIELD_H
#define PACKAGEKIT_BITFIELD_H

#include <QtGlobal>

namespace PackageKit {

class Bitfield
{
public:
    Bitfield ();
    Bitfield (qint64 val);
    ~Bitfield ();

    qint64 operator& (qint64 mask) const;
    qint64 operator&= (qint64 mask);
    qint64 operator| (qint64 mask) const;
    qint64 operator|= (qint64 mask);

    Bitfield operator& (Bitfield mask) const;
    Bitfield operator&= (Bitfield mask);
    Bitfield operator| (Bitfield mask) const;
    Bitfield operator|= (Bitfield mask);

    Bitfield& operator= (const Bitfield& other);

private:
    qint64 m_val;
};

} // End namespace PackageKit

#endif
