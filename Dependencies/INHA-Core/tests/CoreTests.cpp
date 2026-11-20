#include "inha/parameter/Curve.h"
#include "inha/ui/ControlGestures.h"
#include "inha/ui/Theme.h"

#include <cassert>
#include <cmath>

namespace
{
bool near (double left, double right, double tolerance = 1.0e-9)
{
    return std::abs (left - right) <= tolerance;
}
}

int main()
{
    using namespace Inha;

    static_assert (Ui::Theme::panel.nativeColourRef() == 0x00d6e9f0u);
    static_assert (Ui::rangeDragMode ({ true, false, false }) == Ui::RangeDragMode::handle);
    static_assert (Ui::rangeDragMode ({ false, true, false }) == Ui::RangeDragMode::translate);
    static_assert (Ui::rangeDragMode ({ false, false, true }) == Ui::RangeDragMode::symmetric);
    static_assert (Ui::rangeDragMode ({ false, true, true }) == Ui::RangeDragMode::translate);

    assert (near (Ui::draggedValue (0.25, 160.0), 1.0));
    assert (near (Ui::draggedValue (0.25, 60.0, { true, false, false }), 0.35));

    const auto translated = Ui::translateRange ({ 0.2, 0.6 }, 160.0);
    assert (near (translated.minimum, 0.6));
    assert (near (translated.maximum, 1.0));
    assert (near (translated.maximum - translated.minimum, 0.4));

    const auto symmetric = Ui::symmetricRange ({ 0.3, 0.7 }, 16.0);
    assert (near (symmetric.minimum, 0.2));
    assert (near (symmetric.maximum, 0.8));

    const auto muted = Ui::toggleZero (0.63, 0.0);
    const auto restored = Ui::toggleZero (muted.value, muted.rememberedValue);
    assert (near (muted.value, 0.0));
    assert (near (muted.rememberedValue, 0.63));
    assert (near (restored.value, 0.63));

    const auto zeroed = Ui::setZero (0.42, 0.21);
    assert (near (zeroed.value, 0.0));
    assert (near (zeroed.rememberedValue, 0.42));

    const auto clamped = Ui::toggleZero (2.0, -1.0);
    assert (near (clamped.value, 0.0));
    assert (near (clamped.rememberedValue, 1.0));

    const auto logarithmic = Parameter::Curve { Parameter::CurveType::logarithmic, 1.0 };
    assert (near (Parameter::toPlain (0.5, 1.0, 100.0, logarithmic), 10.0));
    assert (near (Parameter::toNormal (10.0, 1.0, 100.0, logarithmic), 0.5));

    const auto progressive = Parameter::Curve { Parameter::CurveType::power,
                                                 3.643856189774725 };
    const auto plain = Parameter::toPlain (0.5, 0.0, 5000.0, progressive);
    assert (near (plain, 400.0, 1.0e-7));
    assert (near (Parameter::toNormal (plain, 0.0, 5000.0, progressive), 0.5));
}
