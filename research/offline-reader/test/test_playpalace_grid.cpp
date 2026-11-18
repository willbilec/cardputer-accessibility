#include "playpalace_grid.h"
#include "playpalace_adpcm.h"

#include <cassert>

int main() {
    using playpalace::Direction;
    using playpalace::navigate;

    // Skippo supports rows of different lengths and clamps to the destination row.
    const std::vector<int> skippo{4, 2, 4, 3};
    assert(navigate(3, 13, 0, 4, skippo, Direction::Down, false) == 5);
    assert(navigate(5, 13, 0, 4, skippo, Direction::Down, false) == 7);
    assert(navigate(3, 13, 0, 4, skippo, Direction::Right, false) == 3);
    assert(navigate(3, 13, 0, 4, skippo, Direction::Down, true) == 12);
    assert(navigate(2, 15, 2, 4, skippo, Direction::Up, false) == 2);
    const std::vector<int> skippoFullHand{4, 1, 4, 5};
    assert(navigate(12, 14, 0, 5, skippoFullHand, Direction::Right, false) == 13);
    assert(navigate(13, 14, 0, 5, skippoFullHand, Direction::Right, false) == 13);
    assert(navigate(3, 14, 0, 5, skippoFullHand, Direction::Right, false) == 3);

    // Fixed desktop grids stay in the column across an incomplete final row.
    const std::vector<int> noRows;
    assert(navigate(4, 9, 2, 3, noRows, Direction::Up, false) == 1);
    assert(navigate(1, 9, 2, 3, noRows, Direction::Down, false) == 2);
    assert(navigate(7, 9, 2, 3, noRows, Direction::Down, false) == 7);
    assert(navigate(4, 9, 2, 3, noRows, Direction::Down, true) == 7);
    assert(navigate(3, 9, 2, 3, noRows, Direction::Right, true) == 4);
    assert(navigate(0, 9, 2, 3, noRows, Direction::Down, true) == 0);
    assert(navigate(0, 0, 0, 0, noRows, Direction::Down, false) == 0);

    playpalace::Adpcm decoder;
    assert(decoder.decode(7) == 11);
    assert(decoder.decode(7) == 41);
    assert(decoder.decode(15) == -22);
    assert(decoder.decode(0) == -13);
}
