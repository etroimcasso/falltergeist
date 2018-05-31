/*
 * Copyright 2012-2018 Falltergeist Developers.
 *
 * This file is part of Falltergeist.
 *
 * Falltergeist is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Falltergeist is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Falltergeist.  If not, see <http://www.gnu.org/licenses/>.
 */

// Related headers
#include "../../VM/Handler/Opcode802BHandler.h"

// C++ standard includes

// Falltergeist includes
#include "../../Logger.h"
#include "../../VM/Script.h"
#include "../../VM/IFalloutStack.h"
#include "VM/IFalloutValue.h"

// Third party includes

namespace Falltergeist {
    namespace VM {
        namespace Handler {
            Opcode802B::Opcode802B(std::shared_ptr<VM::Script> script) : OpcodeHandler(script) {
            }

            void Opcode802B::applyTo(std::shared_ptr<IFalloutContext> context) {
                auto argumentsCounter = context->dataStack()->pop()->asInteger();
                context->returnStack()->push(static_cast<unsigned>(_script->DVARbase()));
                _script->setDVARBase(context->dataStack()->size() - argumentsCounter);
                Logger::debug("SCRIPT") << "[802B] [*] op_push_base = " << _script->DVARbase() << std::endl;
            }

            void Opcode802B::_run() {
                applyTo(_script);
            }
        }
    }
}
