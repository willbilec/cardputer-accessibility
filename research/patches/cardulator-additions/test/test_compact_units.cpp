#include "../src/units_adapter.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main(int argc, char**) {
    const std::string path=argc>1?"lib/gnu-units/units.dat":"accessibility_units.dat";
    if(argc>1){
        auto bad=handleConv(5,"mile","deg",path);
        std::cout<<"Original full-catalogue rejection: "<<bad.errorMsg<<"\n";
        assert(!bad.success);
        return 0;
    }
    auto distance=handleConv(1,"furlong","m",path);
    std::cout<<"Compact furlong: "<<distance.success<<" "<<distance.value<<" "<<distance.errorMsg<<"\n";
    assert(distance.success&&std::abs(distance.value-201.168)<1e-8);
    auto bad=handleConv(5,"mile","deg",path);
    assert(!bad.success&&bad.errorMsg.find("Incompatible")!=std::string::npos);
    auto unknown=handleConv(1,"notarealunit","m",path);
    assert(!unknown.success&&!unknown.errorMsg.empty());
    auto again=handleConv(1,"nauticalmile","m",path);
    assert(again.success&&std::abs(again.value-1852)<1e-8);
    std::cout<<"Compact catalogue conversion, incompatible dimensions, unknown name and recovery passed\n";
}
