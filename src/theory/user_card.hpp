#pragma once

#include "pipeline/sig.hpp"
#include "theory/cards.hpp"

namespace signals::theory {

Card user_block_card(const pipeline::BlockDef& def, const pipeline::PipelineDoc& doc);

}  // namespace signals::theory
