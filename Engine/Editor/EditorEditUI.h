#pragma once
#include "Rice/Benchmark.h"
#include <functional>
#include <utility>

namespace Rice {
using BeforeEditorEdit=std::function<void()>;

// Widgets edit a small native value first. The transaction can capture the
// original object immediately before the first actual mutation, with no scene
// serialization or snapshot work while an inspector is merely being drawn.
template<class T,class F>
bool EditValue(T& value,const BeforeEditorEdit& beforeEdit,F draw){
    RICE_BENCHMARK_FUNCTION();
    auto next=value;
    if(!draw(next))return false;
    if(beforeEdit)beforeEdit();
    value=std::move(next);
    return true;
}
}
