#pragma once

#include <memory>
#include <vector>

#include "tui/screens/decomposition_model.hpp"
#include "tui/screens/designer_model.hpp"
#include "tui/screens/filters_model.hpp"
#include "tui/screens/matrix_model.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/screens/theory_navigator.hpp"
#include "tui/screens/signal_model.hpp"
#include "tui/screens/spectrum_model.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

struct AppModels {
    std::shared_ptr<SignalModel> signal;
    std::shared_ptr<SpectrumModel> spectrum;
    std::shared_ptr<ComparisonSet> comparison;
    std::shared_ptr<DesignerModel> designer;
    std::shared_ptr<DecompositionModel> decomposition;
    std::shared_ptr<MatrixModel> matrix;
    std::shared_ptr<TheoryNavigator> theory;
    std::shared_ptr<PipelineModel> pipeline;
};

AppModels make_app_models();
std::vector<std::shared_ptr<TabScreen>> make_app_screens(UiContext& ui, AppModels& models);

}  // namespace signals::tui
