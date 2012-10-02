#ifndef PACKAGEKIT_BITFIELD_H
#define PACKAGEKIT_BITFIELD_H

#include <QtGlobal>

namespace PackageKit {

class Bitfield
{
public:
    Bitfield ();
    Bitfield (qulonglong val);
    ~Bitfield ();

    qulonglong operator& (qulonglong mask) const;
    qulonglong operator&= (qulonglong mask);
    qulonglong operator| (qulonglong mask) const;
    qulonglong operator|= (qulonglong mask);

    Bitfield operator& (Bitfield mask) const;
    Bitfield operator&= (Bitfield mask);
    Bitfield operator| (Bitfield mask) const;
    Bitfield operator|= (Bitfield mask);

    Bitfield& operator= (const Bitfield& other);

private:
    qulonglong m_val;
};

} // End namespace PackageKit

#endif
