#include "SnvIntersector.hpp"

#include "Bed.hpp"
#include "IResultCollector.hpp"
#include "BedStream.hpp"

SnvIntersector::SnvIntersector(BedStream& a, BedStream& b, IResultCollector& rc)
    : _a(a)
    , _b(b)
    , _rc(rc)
{
}

void SnvIntersector::exec() {
    Bed snvA;
    Bed snvB;
    Bed peek;
    _a >> snvA;
    _b >> snvB;

    while (!_a.eof() && !_b.eof()) {
        // TODO: burn off repeats

        int c = snvA.cmp(snvB);
        if (c < 0) {
            _rc.missA(snvA);
            while (_a.peek(peek) && peek.cmp(snvA) == 0) {
                _a >> snvA;
                _rc.missA(snvA);
            }
            _a >> snvA;
        } else if (c > 0) {
            _rc.missB(snvB);
            while (_b.peek(peek) && peek.cmp(snvB) == 0) {
                _b >> snvB;
                _rc.missB(snvB);
            }
            _b >> snvB;
        } else {
            _rc.hit(snvA, snvB);
            while (_a.peek(peek) && peek.cmp(snvA) == 0) {
                _a >> snvA;
                _rc.hit(snvA, snvB);
            }

            while (_b.peek(peek) && peek.cmp(snvB) == 0) {
                _b >> snvB;
                _rc.hit(snvA, snvB);
            }
            _a >> snvA;
            _b >> snvB;

            // NOTE: do not uncomment this. we do not advance B here because
            // we want to allow for the possibility of repetitions in A.
            // if we advanced B, these would be seen as misses. B will advance
            // naturally once A has passed it
            //
            // _b >> snvB
        }
    }

    while (!_a.eof()) {
        _a >> snvA;
        _rc.missA(snvA);
    }

    while (!_b.eof()) {
        _b >> snvB;
        _rc.missB(snvB);
    }
}
