DexValue Interpreter::RunBytecode(DexFrame* frame, const art::CodeItemDataAccessor& accessor) {
    DexValue return_value;
    const DexMethod* method = frame->method();

    const uint16_t* insns = accessor.Insns();
    uint32_t dex_pc = frame->dex_pc();
    const uint32_t insns_size = accessor.InsnsSizeInCodeUnits();

    while (dex_pc < insns_size) {
        if (instruction_limit_ != UINT64_MAX && ++instructions_executed_ > instruction_limit_) {
            ThrowException("Ljava/lang/InternalError;", "instruction number limit exceeded");
            return return_value;
        }

        // Record dex_pc before running so handlers know the throwing instruction.
        frame->set_dex_pc(dex_pc);

        const Instruction* inst = Instruction::At(insns + dex_pc);
        const uint32_t next_pc =
            dex_pc + static_cast<uint32_t>(inst->SizeInCodeUnits());

        switch (inst->Opcode()) {
            case Instruction::NOP:
                break;

            // Move.
            case Instruction::MOVE:
            case Instruction::MOVE_OBJECT:
                frame->Set(inst->VRegA_12x(), frame->Get(inst->VRegB_12x()));
                break;
            case Instruction::MOVE_FROM16:
            case Instruction::MOVE_OBJECT_FROM16:
                frame->Set(inst->VRegA_22x(), frame->Get(inst->VRegB_22x()));
                break;
            case Instruction::MOVE_16:
            case Instruction::MOVE_OBJECT_16:
                frame->Set(inst->VRegA_32x(), frame->Get(inst->VRegB_32x()));
                break;
            case Instruction::MOVE_WIDE:
                frame->Set(inst->VRegA_12x(), frame->Get(inst->VRegB_12x()));
                break;
            case Instruction::MOVE_WIDE_FROM16:
                frame->Set(inst->VRegA_22x(), frame->Get(inst->VRegB_22x()));
                break;
            case Instruction::MOVE_WIDE_16:
                frame->Set(inst->VRegA_32x(), frame->Get(inst->VRegB_32x()));
                break;

            case Instruction::MOVE_RESULT:
            case Instruction::MOVE_RESULT_WIDE:
            case Instruction::MOVE_RESULT_OBJECT:
                frame->Set(inst->VRegA_11x(), frame->result());
                break;

            // Only valid at the beginning of the catch handler; ExecuteFrame is already set.
            case Instruction::MOVE_EXCEPTION:
                frame->SetRef(inst->VRegA_11x(), frame->caught_exception());
                frame->set_caught_exception(nullptr);
                break;

            // Const.
            case Instruction::CONST_4:
                frame->SetInt(inst->VRegA_11n(), inst->VRegB_11n());
                break;
            case Instruction::CONST_16:
                frame->SetInt(inst->VRegA_21s(), inst->VRegB_21s());
                break;
            case Instruction::CONST:
                frame->SetInt(inst->VRegA_31i(), inst->VRegB_31i());
                break;
            case Instruction::CONST_HIGH16:
                frame->SetInt(inst->VRegA_21h(),
                              static_cast<int32_t>(inst->VRegB_21h()) << 16);
                break;
            case Instruction::CONST_WIDE_16:
                frame->SetLong(inst->VRegA_21s(), inst->VRegB_21s());
                break;
            case Instruction::CONST_WIDE_32:
                frame->SetLong(inst->VRegA_31i(), inst->VRegB_31i());
                break;
            case Instruction::CONST_WIDE:
                frame->SetLong(inst->VRegA_51l(),
                               static_cast<int64_t>(inst->WideVRegB()));
                break;
            case Instruction::CONST_WIDE_HIGH16:
                frame->SetLong(inst->VRegA_21h(),
                               static_cast<int64_t>(inst->VRegB_21h()) << 48);
                break;

            // Return.
            case Instruction::RETURN_VOID:
            case Instruction::RETURN_VOID_NO_BARRIER:
                return return_value;
            case Instruction::RETURN:
            case Instruction::RETURN_WIDE:
            case Instruction::RETURN_OBJECT:
                return frame->Get(inst->VRegA_11x());

            // Compare.
            case Instruction::CMPL_FLOAT:
                frame->SetInt(inst->VRegA_23x(),
                              CompareFloat(frame->GetFloat(inst->VRegB_23x()),
                                           frame->GetFloat(inst->VRegC_23x()), -1));
                break;
            case Instruction::CMPG_FLOAT:
                frame->SetInt(inst->VRegA_23x(),
                              CompareFloat(frame->GetFloat(inst->VRegB_23x()),
                                           frame->GetFloat(inst->VRegC_23x()), 1));
                break;
            case Instruction::CMPL_DOUBLE:
                frame->SetInt(inst->VRegA_23x(),
                              CompareFloat(frame->GetDouble(inst->VRegB_23x()),
                                           frame->GetDouble(inst->VRegC_23x()), -1));
                break;
            case Instruction::CMPG_DOUBLE:
                frame->SetInt(inst->VRegA_23x(),
                              CompareFloat(frame->GetDouble(inst->VRegB_23x()),
                                           frame->GetDouble(inst->VRegC_23x()), 1));
                break;
            case Instruction::CMP_LONG:
                frame->SetInt(inst->VRegA_23x(),
                              CompareLong(frame->GetLong(inst->VRegB_23x()),
                                          frame->GetLong(inst->VRegC_23x())));
                break;

            // Jump.
            case Instruction::GOTO:
                dex_pc += inst->VRegA_10t();
                continue;
            case Instruction::GOTO_16:
                dex_pc += inst->VRegA_20t();
                continue;
            case Instruction::GOTO_32:
                dex_pc += inst->VRegA_30t();
                continue;

            case Instruction::IF_EQ: case Instruction::IF_NE:
            case Instruction::IF_LT: case Instruction::IF_GE:
            case Instruction::IF_GT: case Instruction::IF_LE: {
                const int32_t a = frame->GetInt(inst->VRegA_22t());
                const int32_t b = frame->GetInt(inst->VRegB_22t());
                bool taken = false;
                switch (inst->Opcode()) {
                    case Instruction::IF_EQ: taken = a == b; break;
                    case Instruction::IF_NE: taken = a != b; break;
                    case Instruction::IF_LT: taken = a < b; break;
                    case Instruction::IF_GE: taken = a >= b; break;
                    case Instruction::IF_GT: taken = a > b; break;
                    default:                 taken = a <= b; break;
                }
                if (taken) {
                    dex_pc += inst->VRegC_22t();
                    continue;
                }
                break;
            }

            case Instruction::IF_EQZ: case Instruction::IF_NEZ:
            case Instruction::IF_LTZ: case Instruction::IF_GEZ:
            case Instruction::IF_GTZ: case Instruction::IF_LEZ: {
                const int32_t a = frame->GetInt(inst->VRegA_21t());
                bool taken = false;
                switch (inst->Opcode()) {
                    case Instruction::IF_EQZ: taken = a == 0; break;
                    case Instruction::IF_NEZ: taken = a != 0; break;
                    case Instruction::IF_LTZ: taken = a < 0; break;
                    case Instruction::IF_GEZ: taken = a >= 0; break;
                    case Instruction::IF_GTZ: taken = a > 0; break;
                    default:                  taken = a <= 0; break;
                }
                if (taken) {
                    dex_pc += inst->VRegB_21t();
                    continue;
                }
                break;
            }

            // An operand.
            case Instruction::NEG_INT:
                // Java's neg-int wraps: -INT_MIN is INT_MIN. Written unsigned because the
                // signed negation of INT_MIN is UB in C++ and clang is free to exploit that.
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(
                                  0u - static_cast<uint32_t>(frame->GetInt(inst->VRegB_12x()))));
                break;
            case Instruction::NOT_INT:
                frame->SetInt(inst->VRegA_12x(), ~frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::NEG_LONG:
                // Same wrap for long: -LONG_MIN is LONG_MIN.
                frame->SetLong(inst->VRegA_12x(),
                               static_cast<int64_t>(
                                   0ull - static_cast<uint64_t>(frame->GetLong(inst->VRegB_12x()))));
                break;
            case Instruction::NOT_LONG:
                frame->SetLong(inst->VRegA_12x(), ~frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::NEG_FLOAT:
                frame->SetFloat(inst->VRegA_12x(), -frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::NEG_DOUBLE:
                frame->SetDouble(inst->VRegA_12x(), -frame->GetDouble(inst->VRegB_12x()));
                break;

            // Change style.
            case Instruction::INT_TO_LONG:
                frame->SetLong(inst->VRegA_12x(), frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::INT_TO_FLOAT:
                frame->SetFloat(inst->VRegA_12x(),
                                static_cast<float>(frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::INT_TO_DOUBLE:
                frame->SetDouble(inst->VRegA_12x(), frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::LONG_TO_INT:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(frame->GetLong(inst->VRegB_12x())));
                break;
            case Instruction::LONG_TO_FLOAT:
                frame->SetFloat(inst->VRegA_12x(),
                                static_cast<float>(frame->GetLong(inst->VRegB_12x())));
                break;
            case Instruction::LONG_TO_DOUBLE:
                frame->SetDouble(inst->VRegA_12x(),
                                 static_cast<double>(frame->GetLong(inst->VRegB_12x())));
                break;
            case Instruction::FLOAT_TO_INT:
                frame->SetInt(inst->VRegA_12x(),
                              JavaFloatToInt<int32_t>(frame->GetFloat(inst->VRegB_12x())));
                break;
            case Instruction::FLOAT_TO_LONG:
                frame->SetLong(inst->VRegA_12x(),
                               JavaFloatToInt<int64_t>(frame->GetFloat(inst->VRegB_12x())));
                break;
            case Instruction::FLOAT_TO_DOUBLE:
                frame->SetDouble(inst->VRegA_12x(), frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::DOUBLE_TO_INT:
                frame->SetInt(inst->VRegA_12x(),
                              JavaFloatToInt<int32_t>(frame->GetDouble(inst->VRegB_12x())));
                break;
            case Instruction::DOUBLE_TO_LONG:
                frame->SetLong(inst->VRegA_12x(),
                               JavaFloatToInt<int64_t>(frame->GetDouble(inst->VRegB_12x())));
                break;
            case Instruction::DOUBLE_TO_FLOAT:
                frame->SetFloat(inst->VRegA_12x(),
                                static_cast<float>(frame->GetDouble(inst->VRegB_12x())));
                break;
            case Instruction::INT_TO_BYTE:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int8_t>(frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::INT_TO_CHAR:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<uint16_t>(frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::INT_TO_SHORT:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int16_t>(frame->GetInt(inst->VRegB_12x())));
                break;

            // Int arithmetic, two operands (23x).
            case Instruction::ADD_INT:
                frame->SetInt(inst->VRegA_23x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_23x())) +
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegC_23x()))));
                break;
            case Instruction::SUB_INT:
                frame->SetInt(inst->VRegA_23x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_23x())) -
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegC_23x()))));
                break;
            case Instruction::MUL_INT:
                frame->SetInt(inst->VRegA_23x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_23x())) *
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegC_23x()))));
                break;
            case Instruction::DIV_INT: {
                const int32_t b = frame->GetInt(inst->VRegC_23x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_23x(), JavaIntDiv(frame->GetInt(inst->VRegB_23x()), b));
                break;
            }
            case Instruction::REM_INT: {
                const int32_t b = frame->GetInt(inst->VRegC_23x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_23x(), JavaIntRem(frame->GetInt(inst->VRegB_23x()), b));
                break;
            }
            case Instruction::AND_INT:
                frame->SetInt(inst->VRegA_23x(), frame->GetInt(inst->VRegB_23x()) &
                                                     frame->GetInt(inst->VRegC_23x()));
                break;
            case Instruction::OR_INT:
                frame->SetInt(inst->VRegA_23x(), frame->GetInt(inst->VRegB_23x()) |
                                                     frame->GetInt(inst->VRegC_23x()));
                break;
            case Instruction::XOR_INT:
                frame->SetInt(inst->VRegA_23x(), frame->GetInt(inst->VRegB_23x()) ^
                                                     frame->GetInt(inst->VRegC_23x()));
                break;
            case Instruction::SHL_INT:
                frame->SetInt(inst->VRegA_23x(), JavaShl(frame->GetInt(inst->VRegB_23x()),
                                                         frame->GetInt(inst->VRegC_23x())));
                break;
            case Instruction::SHR_INT:
                frame->SetInt(inst->VRegA_23x(), JavaShr(frame->GetInt(inst->VRegB_23x()),
                                                         frame->GetInt(inst->VRegC_23x())));
                break;
            case Instruction::USHR_INT:
                frame->SetInt(inst->VRegA_23x(), JavaUshr(frame->GetInt(inst->VRegB_23x()),
                                                          frame->GetInt(inst->VRegC_23x())));
                break;

            // Long arithmetic.
            case Instruction::ADD_LONG:
                frame->SetLong(inst->VRegA_23x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_23x())) +
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegC_23x()))));
                break;
            case Instruction::SUB_LONG:
                frame->SetLong(inst->VRegA_23x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_23x())) -
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegC_23x()))));
                break;
            case Instruction::MUL_LONG:
                frame->SetLong(inst->VRegA_23x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_23x())) *
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegC_23x()))));
                break;
            case Instruction::DIV_LONG: {
                const int64_t b = frame->GetLong(inst->VRegC_23x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetLong(inst->VRegA_23x(), JavaLongDiv(frame->GetLong(inst->VRegB_23x()), b));
                break;
            }
            case Instruction::REM_LONG: {
                const int64_t b = frame->GetLong(inst->VRegC_23x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetLong(inst->VRegA_23x(), JavaLongRem(frame->GetLong(inst->VRegB_23x()), b));
                break;
            }
            case Instruction::AND_LONG:
                frame->SetLong(inst->VRegA_23x(), frame->GetLong(inst->VRegB_23x()) &
                                                      frame->GetLong(inst->VRegC_23x()));
                break;
            case Instruction::OR_LONG:
                frame->SetLong(inst->VRegA_23x(), frame->GetLong(inst->VRegB_23x()) |
                                                      frame->GetLong(inst->VRegC_23x()));
                break;
            case Instruction::XOR_LONG:
                frame->SetLong(inst->VRegA_23x(), frame->GetLong(inst->VRegB_23x()) ^
                                                      frame->GetLong(inst->VRegC_23x()));
                break;
            case Instruction::SHL_LONG:
                frame->SetLong(inst->VRegA_23x(), JavaShlLong(frame->GetLong(inst->VRegB_23x()),
                                                              frame->GetInt(inst->VRegC_23x())));
                break;
            case Instruction::SHR_LONG:
                frame->SetLong(inst->VRegA_23x(), JavaShrLong(frame->GetLong(inst->VRegB_23x()),
                                                              frame->GetInt(inst->VRegC_23x())));
                break;
            case Instruction::USHR_LONG:
                frame->SetLong(inst->VRegA_23x(), JavaUshrLong(frame->GetLong(inst->VRegB_23x()),
                                                               frame->GetInt(inst->VRegC_23x())));
                break;

            // Float/double arithmetic.
            case Instruction::ADD_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), frame->GetFloat(inst->VRegB_23x()) +
                                                       frame->GetFloat(inst->VRegC_23x()));
                break;
            case Instruction::SUB_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), frame->GetFloat(inst->VRegB_23x()) -
                                                       frame->GetFloat(inst->VRegC_23x()));
                break;
            case Instruction::MUL_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), frame->GetFloat(inst->VRegB_23x()) *
                                                       frame->GetFloat(inst->VRegC_23x()));
                break;
            case Instruction::DIV_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), frame->GetFloat(inst->VRegB_23x()) /
                                                       frame->GetFloat(inst->VRegC_23x()));
                break;
            case Instruction::REM_FLOAT:
                frame->SetFloat(inst->VRegA_23x(), std::fmod(frame->GetFloat(inst->VRegB_23x()),
                                                             frame->GetFloat(inst->VRegC_23x())));
                break;
            case Instruction::ADD_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), frame->GetDouble(inst->VRegB_23x()) +
                                                        frame->GetDouble(inst->VRegC_23x()));
                break;
            case Instruction::SUB_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), frame->GetDouble(inst->VRegB_23x()) -
                                                        frame->GetDouble(inst->VRegC_23x()));
                break;
            case Instruction::MUL_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), frame->GetDouble(inst->VRegB_23x()) *
                                                        frame->GetDouble(inst->VRegC_23x()));
                break;
            case Instruction::DIV_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), frame->GetDouble(inst->VRegB_23x()) /
                                                        frame->GetDouble(inst->VRegC_23x()));
                break;
            case Instruction::REM_DOUBLE:
                frame->SetDouble(inst->VRegA_23x(), std::fmod(frame->GetDouble(inst->VRegB_23x()),
                                                              frame->GetDouble(inst->VRegC_23x())));
                break;

            // Form /2addr: destination is also the first operand.
            case Instruction::ADD_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegA_12x())) +
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_12x()))));
                break;
            case Instruction::SUB_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegA_12x())) -
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_12x()))));
                break;
            case Instruction::MUL_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegA_12x())) *
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_12x()))));
                break;
            case Instruction::DIV_INT_2ADDR: {
                const int32_t b = frame->GetInt(inst->VRegB_12x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_12x(), JavaIntDiv(frame->GetInt(inst->VRegA_12x()), b));
                break;
            }
            case Instruction::REM_INT_2ADDR: {
                const int32_t b = frame->GetInt(inst->VRegB_12x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_12x(), JavaIntRem(frame->GetInt(inst->VRegA_12x()), b));
                break;
            }
            case Instruction::AND_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              frame->GetInt(inst->VRegA_12x()) & frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::OR_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              frame->GetInt(inst->VRegA_12x()) | frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::XOR_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(),
                              frame->GetInt(inst->VRegA_12x()) ^ frame->GetInt(inst->VRegB_12x()));
                break;
            case Instruction::SHL_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(), JavaShl(frame->GetInt(inst->VRegA_12x()),
                                                         frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::SHR_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(), JavaShr(frame->GetInt(inst->VRegA_12x()),
                                                         frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::USHR_INT_2ADDR:
                frame->SetInt(inst->VRegA_12x(), JavaUshr(frame->GetInt(inst->VRegA_12x()),
                                                          frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::ADD_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegA_12x())) +
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_12x()))));
                break;
            case Instruction::SUB_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegA_12x())) -
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_12x()))));
                break;
            case Instruction::MUL_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(),
                               static_cast<int64_t>(
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegA_12x())) *
                                   static_cast<uint64_t>(frame->GetLong(inst->VRegB_12x()))));
                break;
            case Instruction::DIV_LONG_2ADDR: {
                const int64_t b = frame->GetLong(inst->VRegB_12x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetLong(inst->VRegA_12x(), JavaLongDiv(frame->GetLong(inst->VRegA_12x()), b));
                break;
            }
            case Instruction::REM_LONG_2ADDR: {
                const int64_t b = frame->GetLong(inst->VRegB_12x());
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetLong(inst->VRegA_12x(), JavaLongRem(frame->GetLong(inst->VRegA_12x()), b));
                break;
            }
            case Instruction::AND_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), frame->GetLong(inst->VRegA_12x()) &
                                                      frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::OR_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), frame->GetLong(inst->VRegA_12x()) |
                                                      frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::XOR_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), frame->GetLong(inst->VRegA_12x()) ^
                                                      frame->GetLong(inst->VRegB_12x()));
                break;
            case Instruction::SHL_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), JavaShlLong(frame->GetLong(inst->VRegA_12x()),
                                                              frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::SHR_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), JavaShrLong(frame->GetLong(inst->VRegA_12x()),
                                                              frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::USHR_LONG_2ADDR:
                frame->SetLong(inst->VRegA_12x(), JavaUshrLong(frame->GetLong(inst->VRegA_12x()),
                                                               frame->GetInt(inst->VRegB_12x())));
                break;
            case Instruction::ADD_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), frame->GetFloat(inst->VRegA_12x()) +
                                                       frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::SUB_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), frame->GetFloat(inst->VRegA_12x()) -
                                                       frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::MUL_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), frame->GetFloat(inst->VRegA_12x()) *
                                                       frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::DIV_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), frame->GetFloat(inst->VRegA_12x()) /
                                                       frame->GetFloat(inst->VRegB_12x()));
                break;
            case Instruction::REM_FLOAT_2ADDR:
                frame->SetFloat(inst->VRegA_12x(), std::fmod(frame->GetFloat(inst->VRegA_12x()),
                                                             frame->GetFloat(inst->VRegB_12x())));
                break;
            case Instruction::ADD_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), frame->GetDouble(inst->VRegA_12x()) +
                                                        frame->GetDouble(inst->VRegB_12x()));
                break;
            case Instruction::SUB_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), frame->GetDouble(inst->VRegA_12x()) -
                                                        frame->GetDouble(inst->VRegB_12x()));
                break;
            case Instruction::MUL_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), frame->GetDouble(inst->VRegA_12x()) *
                                                        frame->GetDouble(inst->VRegB_12x()));
                break;
            case Instruction::DIV_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), frame->GetDouble(inst->VRegA_12x()) /
                                                        frame->GetDouble(inst->VRegB_12x()));
                break;
            case Instruction::REM_DOUBLE_2ADDR:
                frame->SetDouble(inst->VRegA_12x(), std::fmod(frame->GetDouble(inst->VRegA_12x()),
                                                              frame->GetDouble(inst->VRegB_12x())));
                break;

            // Int with constant.
            case Instruction::ADD_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22s())) +
                                  static_cast<uint32_t>(inst->VRegC_22s())));
                break;
            case Instruction::RSUB_INT:
                frame->SetInt(inst->VRegA_22s(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(inst->VRegC_22s()) -
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22s()))));
                break;
            case Instruction::MUL_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22s())) *
                                  static_cast<uint32_t>(inst->VRegC_22s())));
                break;
            case Instruction::DIV_INT_LIT16: {
                const int32_t b = inst->VRegC_22s();
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_22s(), JavaIntDiv(frame->GetInt(inst->VRegB_22s()), b));
                break;
            }
            case Instruction::REM_INT_LIT16: {
                const int32_t b = inst->VRegC_22s();
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_22s(), JavaIntRem(frame->GetInt(inst->VRegB_22s()), b));
                break;
            }
            case Instruction::AND_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              frame->GetInt(inst->VRegB_22s()) & inst->VRegC_22s());
                break;
            case Instruction::OR_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              frame->GetInt(inst->VRegB_22s()) | inst->VRegC_22s());
                break;
            case Instruction::XOR_INT_LIT16:
                frame->SetInt(inst->VRegA_22s(),
                              frame->GetInt(inst->VRegB_22s()) ^ inst->VRegC_22s());
                break;

            case Instruction::ADD_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22b())) +
                                  static_cast<uint32_t>(inst->VRegC_22b())));
                break;
            case Instruction::RSUB_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(inst->VRegC_22b()) -
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22b()))));
                break;
            case Instruction::MUL_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              static_cast<int32_t>(
                                  static_cast<uint32_t>(frame->GetInt(inst->VRegB_22b())) *
                                  static_cast<uint32_t>(inst->VRegC_22b())));
                break;
            case Instruction::DIV_INT_LIT8: {
                const int32_t b = inst->VRegC_22b();
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_22b(), JavaIntDiv(frame->GetInt(inst->VRegB_22b()), b));
                break;
            }
            case Instruction::REM_INT_LIT8: {
                const int32_t b = inst->VRegC_22b();
                if (b == 0) {
                    ThrowException("Ljava/lang/ArithmeticException;", "divide by zero");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_22b(), JavaIntRem(frame->GetInt(inst->VRegB_22b()), b));
                break;
            }
            case Instruction::AND_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              frame->GetInt(inst->VRegB_22b()) & inst->VRegC_22b());
                break;
            case Instruction::OR_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              frame->GetInt(inst->VRegB_22b()) | inst->VRegC_22b());
                break;
            case Instruction::XOR_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              frame->GetInt(inst->VRegB_22b()) ^ inst->VRegC_22b());
                break;
            case Instruction::SHL_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              JavaShl(frame->GetInt(inst->VRegB_22b()), inst->VRegC_22b()));
                break;
            case Instruction::SHR_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              JavaShr(frame->GetInt(inst->VRegB_22b()), inst->VRegC_22b()));
                break;
            case Instruction::USHR_INT_LIT8:
                frame->SetInt(inst->VRegA_22b(),
                              JavaUshr(frame->GetInt(inst->VRegB_22b()), inst->VRegC_22b()));
                break;

            // Constant string and constant class.
            case Instruction::CONST_STRING: {
                const uint32_t sidx = inst->VRegB_21c();
                if (sidx >= method->dex_file->NumStringIds()) {
                    ThrowException("Ljava/lang/InternalError;", "const-string index out of range");
                    return return_value;
                }
                const char* s = method->dex_file->StringDataByIdx(
                    art::dex::StringIndex(sidx));
                frame->SetRef(inst->VRegA_21c(), linker_->InternString(s));
                break;
            }
            case Instruction::CONST_STRING_JUMBO: {
                const char* s = method->dex_file->StringDataByIdx(
                    art::dex::StringIndex(inst->VRegB_31c()));
                frame->SetRef(inst->VRegA_31c(), linker_->InternString(s));
                break;
            }
            case Instruction::CONST_CLASS: {
                DexClass* klass = ResolveClass(method, inst->VRegB_21c());
                if (klass == nullptr) {
                    ThrowException("Ljava/lang/ClassNotFoundException;", "const-class");
                    return return_value;
                }
                frame->SetRef(inst->VRegA_21c(), linker_->GetClassObject(klass));
                break;
            }

            // Create objects and arrays.
            case Instruction::NEW_INSTANCE: {
                DexClass* klass = ResolveClass(method, inst->VRegB_21c());
                if (klass == nullptr) {
                    ThrowException("Ljava/lang/ClassNotFoundException;", "new-instance");
                    return return_value;
                }
                // Stub instantiation fails fast with NoClassDefFoundError.
                if (klass->is_stub) {
                    ThrowException("Ljava/lang/NoClassDefFoundError;", klass->PrettyName());
                    return return_value;
                }
                EnsureInitialized(klass);
                if (HasPendingException()) return return_value;
                DexObject* obj = linker_->AllocObject(klass);
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/OutOfMemoryError;", klass->PrettyName());
                    return return_value;
                }
                frame->SetRef(inst->VRegA_21c(), obj);
                break;
            }
            case Instruction::NEW_ARRAY: {
                const int32_t length = frame->GetInt(inst->VRegB_22c());
                if (length < 0) {
                    ThrowException("Ljava/lang/NegativeArraySizeException;",
                                   std::to_string(length));
                    return return_value;
                }
                DexClass* array_class = ResolveClass(method, inst->VRegC_22c());
                if (array_class == nullptr) {
                    ThrowException("Ljava/lang/ClassNotFoundException;", "new-array");
                    return return_value;
                }
                DexArray* arr = linker_->AllocArray(array_class, length);
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/OutOfMemoryError;", "new-array");
                    return return_value;
                }
                frame->SetRef(inst->VRegA_22c(), arr);
                break;
            }
            case Instruction::FILLED_NEW_ARRAY:
                if (!FilledNewArray(frame, inst, /*is_range=*/false)) {
                    return return_value;
                }
                break;
            case Instruction::FILLED_NEW_ARRAY_RANGE:
                if (!FilledNewArray(frame, inst, /*is_range=*/true)) {
                    return return_value;
                }
                break;
            case Instruction::ARRAY_LENGTH: {
                auto* arr = static_cast<DexArray*>(frame->GetRef(inst->VRegB_12x()));
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "array-length on null");
                    return return_value;
                }
                frame->SetInt(inst->VRegA_12x(), arr->length);
                break;
            }

            // Check the type.
            case Instruction::INSTANCE_OF: {
                DexObject* obj = frame->GetRef(inst->VRegB_22c());
                DexClass* target = ResolveClass(method, inst->VRegC_22c());
                const bool ok = obj != nullptr && obj->clazz != nullptr && target != nullptr &&
                                obj->clazz->IsSubClassOf(target);
                frame->SetInt(inst->VRegA_22c(), ok ? 1 : 0);
                break;
            }
            case Instruction::CHECK_CAST: {
                DexObject* obj = frame->GetRef(inst->VRegA_21c());
                if (obj == nullptr) break;  // cast null is always valid
                DexClass* target = ResolveClass(method, inst->VRegB_21c());
                if (target == nullptr || obj->clazz == nullptr ||
                    !obj->clazz->IsSubClassOf(target)) {
                    ThrowException("Ljava/lang/ClassCastException;",
                                   (obj->clazz != nullptr ? obj->clazz->PrettyName() : "?") +
                                       " not " +
                                       (target != nullptr ? target->PrettyName() : "?"));
                    return return_value;
                }
                break;
            }

            // Field instance.
            case Instruction::IGET:
            case Instruction::IGET_WIDE:
            case Instruction::IGET_OBJECT:
            case Instruction::IGET_BOOLEAN:
            case Instruction::IGET_BYTE:
            case Instruction::IGET_CHAR:
            case Instruction::IGET_SHORT: {
                DexObject* obj = frame->GetRef(inst->VRegB_22c());
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "iget on null");
                    return return_value;
                }
                DexField* field = ResolveField(method, inst->VRegC_22c(), /*is_static=*/false);
                uint32_t igetWidth = 4;
                switch (inst->Opcode()) {
                    case Instruction::IGET_OBJECT:
                        igetWidth = sizeof(DexObject*);
                        break;
                    case Instruction::IGET_WIDE:
                        igetWidth = 8;
                        break;
                    case Instruction::IGET_BOOLEAN:
                    case Instruction::IGET_BYTE:
                        igetWidth = 1;
                        break;
                    case Instruction::IGET_CHAR:
                    case Instruction::IGET_SHORT:
                        igetWidth = 2;
                        break;
                    default:
                        break;
                }
                if (field == nullptr || !CheckInstanceField(obj, field, igetWidth)) {
                    if (field == nullptr) {
                        ThrowException("Ljava/lang/NoSuchFieldError;",
                                       DescribeFieldRef(method, inst->VRegC_22c(), "iget"));
                    }
                    return return_value;
                }
                const uint32_t off = field->offset_or_slot;
                const uint32_t vreg = inst->VRegA_22c();
                switch (inst->Opcode()) {
                    case Instruction::IGET_BOOLEAN:
                    case Instruction::IGET_BYTE:
                        frame->SetInt(vreg, obj->GetField<int8_t>(off));
                        break;
                    case Instruction::IGET_CHAR:
                        frame->SetInt(vreg, obj->GetField<uint16_t>(off));
                        break;
                    case Instruction::IGET_SHORT:
                        frame->SetInt(vreg, obj->GetField<int16_t>(off));
                        break;
                    case Instruction::IGET_WIDE:
                        frame->SetLong(vreg, obj->GetField<int64_t>(off));
                        break;
                    case Instruction::IGET_OBJECT:
                        frame->SetRef(vreg, obj->GetField<DexObject*>(off));
                        break;
                    default:
                        frame->SetInt(vreg, obj->GetField<int32_t>(off));
                        break;
                }
                break;
            }

            case Instruction::IPUT:
            case Instruction::IPUT_WIDE:
            case Instruction::IPUT_OBJECT:
            case Instruction::IPUT_BOOLEAN:
            case Instruction::IPUT_BYTE:
            case Instruction::IPUT_CHAR:
            case Instruction::IPUT_SHORT: {
                DexObject* obj = frame->GetRef(inst->VRegB_22c());
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "iput on null");
                    return return_value;
                }
                DexField* field = ResolveField(method, inst->VRegC_22c(), /*is_static=*/false);
                uint32_t iputWidth = 4;
                switch (inst->Opcode()) {
                    case Instruction::IPUT_OBJECT:
                        iputWidth = sizeof(DexObject*);
                        break;
                    case Instruction::IPUT_WIDE:
                        iputWidth = 8;
                        break;
                    case Instruction::IPUT_BOOLEAN:
                    case Instruction::IPUT_BYTE:
                        iputWidth = 1;
                        break;
                    case Instruction::IPUT_CHAR:
                    case Instruction::IPUT_SHORT:
                        iputWidth = 2;
                        break;
                    default:
                        break;
                }
                if (field == nullptr || !CheckInstanceField(obj, field, iputWidth)) {
                    if (field == nullptr) {
                        ThrowException("Ljava/lang/NoSuchFieldError;",
                                       DescribeFieldRef(method, inst->VRegC_22c(), "iput"));
                    }
                    return return_value;
                }
                const uint32_t off = field->offset_or_slot;
                const uint32_t vreg = inst->VRegA_22c();
                switch (inst->Opcode()) {
                    case Instruction::IPUT_BOOLEAN:
                    case Instruction::IPUT_BYTE:
                        obj->SetField<int8_t>(off, static_cast<int8_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::IPUT_CHAR:
                        obj->SetField<uint16_t>(off, static_cast<uint16_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::IPUT_SHORT:
                        obj->SetField<int16_t>(off, static_cast<int16_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::IPUT_WIDE:
                        obj->SetField<int64_t>(off, frame->GetLong(vreg));
                        break;
                    case Instruction::IPUT_OBJECT:
                        obj->SetField<DexObject*>(off, frame->GetRef(vreg));
                        break;
                    default:
                        obj->SetField<int32_t>(off, frame->GetInt(vreg));
                        break;
                }
                break;
            }

            // Field static.
            case Instruction::SGET:
            case Instruction::SGET_WIDE:
            case Instruction::SGET_OBJECT:
            case Instruction::SGET_BOOLEAN:
            case Instruction::SGET_BYTE:
            case Instruction::SGET_CHAR:
            case Instruction::SGET_SHORT: {
                DexField* field = ResolveField(method, inst->VRegB_21c(), /*is_static=*/true);
                if (field == nullptr || field->declaring_class == nullptr) {
                    ThrowException("Ljava/lang/NoSuchFieldError;",
                                   DescribeFieldRef(method, inst->VRegB_21c(), "sget"));
                    return return_value;
                }
                EnsureInitialized(field->declaring_class);
                if (HasPendingException()) return return_value;
                auto& values = field->declaring_class->static_values;
                const uint32_t slot = field->offset_or_slot;
                if (slot >= values.size()) {
                    ThrowException("Ljava/lang/NoSuchFieldError;",
                                   DescribeFieldRef(method, inst->VRegB_21c(), "sget slot out of range"));
                    return return_value;
                }
                frame->Set(inst->VRegA_21c(), values[slot]);
                break;
            }

            case Instruction::SPUT:
            case Instruction::SPUT_WIDE:
            case Instruction::SPUT_OBJECT:
            case Instruction::SPUT_BOOLEAN:
            case Instruction::SPUT_BYTE:
            case Instruction::SPUT_CHAR:
            case Instruction::SPUT_SHORT: {
                DexField* field = ResolveField(method, inst->VRegB_21c(), /*is_static=*/true);
                if (field == nullptr || field->declaring_class == nullptr) {
                    ThrowException("Ljava/lang/NoSuchFieldError;",
                                   DescribeFieldRef(method, inst->VRegB_21c(), "sput"));
                    return return_value;
                }
                EnsureInitialized(field->declaring_class);
                if (HasPendingException()) return return_value;
                auto& values = field->declaring_class->static_values;
                const uint32_t slot = field->offset_or_slot;
                if (slot >= values.size()) {
                    ThrowException("Ljava/lang/NoSuchFieldError;",
                                   DescribeFieldRef(method, inst->VRegB_21c(), "sput slot out of range"));
                    return return_value;
                }
                values[slot] = frame->Get(inst->VRegA_21c());
                break;
            }

            // Array element.
            case Instruction::AGET:
            case Instruction::AGET_WIDE:
            case Instruction::AGET_OBJECT:
            case Instruction::AGET_BOOLEAN:
            case Instruction::AGET_BYTE:
            case Instruction::AGET_CHAR:
            case Instruction::AGET_SHORT: {
                auto* arr = static_cast<DexArray*>(frame->GetRef(inst->VRegB_23x()));
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "aget on null");
                    return return_value;
                }
                const int32_t index = frame->GetInt(inst->VRegC_23x());
                if (index < 0 || index >= arr->length) {
                    ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;",
                                   "index " + std::to_string(index) + " length " +
                                       std::to_string(arr->length));
                    return return_value;
                }
                const uint32_t vreg = inst->VRegA_23x();
                switch (inst->Opcode()) {
                    case Instruction::AGET_BOOLEAN:
                    case Instruction::AGET_BYTE:
                        frame->SetInt(vreg, arr->Get<int8_t>(index));
                        break;
                    case Instruction::AGET_CHAR:
                        frame->SetInt(vreg, arr->Get<uint16_t>(index));
                        break;
                    case Instruction::AGET_SHORT:
                        frame->SetInt(vreg, arr->Get<int16_t>(index));
                        break;
                    case Instruction::AGET_WIDE:
                        frame->SetLong(vreg, arr->Get<int64_t>(index));
                        break;
                    case Instruction::AGET_OBJECT:
                        frame->SetRef(vreg, arr->Get<DexObject*>(index));
                        break;
                    default:
                        frame->SetInt(vreg, arr->Get<int32_t>(index));
                        break;
                }
                break;
            }

            case Instruction::APUT:
            case Instruction::APUT_WIDE:
            case Instruction::APUT_OBJECT:
            case Instruction::APUT_BOOLEAN:
            case Instruction::APUT_BYTE:
            case Instruction::APUT_CHAR:
            case Instruction::APUT_SHORT: {
                auto* arr = static_cast<DexArray*>(frame->GetRef(inst->VRegB_23x()));
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "aput on null");
                    return return_value;
                }
                const int32_t index = frame->GetInt(inst->VRegC_23x());
                if (index < 0 || index >= arr->length) {
                    ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;",
                                   "index " + std::to_string(index) + " length " +
                                       std::to_string(arr->length));
                    return return_value;
                }
                const uint32_t vreg = inst->VRegA_23x();
                switch (inst->Opcode()) {
                    case Instruction::APUT_BOOLEAN:
                    case Instruction::APUT_BYTE:
                        arr->Set<int8_t>(index, static_cast<int8_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::APUT_CHAR:
                        arr->Set<uint16_t>(index, static_cast<uint16_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::APUT_SHORT:
                        arr->Set<int16_t>(index, static_cast<int16_t>(frame->GetInt(vreg)));
                        break;
                    case Instruction::APUT_WIDE:
                        arr->Set<int64_t>(index, frame->GetLong(vreg));
                        break;
                    case Instruction::APUT_OBJECT:
                        arr->Set<DexObject*>(index, frame->GetRef(vreg));
                        break;
                    default:
                        arr->Set<int32_t>(index, frame->GetInt(vreg));
                        break;
                }
                break;
            }

            // Call method.
            case Instruction::INVOKE_VIRTUAL:
            case Instruction::INVOKE_SUPER:
            case Instruction::INVOKE_DIRECT:
            case Instruction::INVOKE_STATIC:
            case Instruction::INVOKE_INTERFACE:
                if (!InvokeMethod(frame, inst, /*is_range=*/false, inst->Opcode())) {
                    return return_value;
                }
                break;
            case Instruction::INVOKE_VIRTUAL_RANGE:
            case Instruction::INVOKE_SUPER_RANGE:
            case Instruction::INVOKE_DIRECT_RANGE:
            case Instruction::INVOKE_STATIC_RANGE:
            case Instruction::INVOKE_INTERFACE_RANGE:
                if (!InvokeMethod(frame, inst, /*is_range=*/true, inst->Opcode())) {
                    return return_value;
                }
                break;

            // Switch. Payload and targets are validated against the code item:
            // a crafted offset/count otherwise reads out of bounds and escapes
            // the PC from the method. Units are 16-bit code units throughout.
            case Instruction::PACKED_SWITCH: {
                const int32_t test = frame->GetInt(inst->VRegA_31t());
                const int32_t off = inst->VRegB_31t();
                const uint32_t base = static_cast<uint32_t>(dex_pc);
                const uint32_t pbase = base + static_cast<uint32_t>(off);
                // Header is ident + size + first_key = 4 units.
                if (off < 0 || pbase + 4 > insns_size) {
                    ThrowException("Ljava/lang/InternalError;", "packed-switch payload out of range");
                    return return_value;
                }
                const uint16_t* payload = insns + pbase;
                const auto* ps =
                    reinterpret_cast<const Instruction::PackedSwitchPayload*>(payload);
                const int32_t delta = test - ps->first_key;
                if (delta >= 0 && delta < static_cast<int32_t>(ps->case_count) &&
                    pbase + 4u + static_cast<uint32_t>(delta) * 2u + 2u <= insns_size) {
                    const int32_t target = ps->targets[delta];
                    const int64_t dest = static_cast<int64_t>(base) + target;
                    if (dest >= 0 && dest <= static_cast<int64_t>(insns_size)) {
                        dex_pc = static_cast<uint32_t>(dest);
                        continue;
                    }
                }
                break;
            }
            case Instruction::SPARSE_SWITCH: {
                const int32_t test = frame->GetInt(inst->VRegA_31t());
                const int32_t off = inst->VRegB_31t();
                const uint32_t base = static_cast<uint32_t>(dex_pc);
                const uint32_t pbase = base + static_cast<uint32_t>(off);
                // Header is ident + case_count = 2 units; keys/targets follow.
                if (off < 0 || pbase + 2 > insns_size) {
                    ThrowException("Ljava/lang/InternalError;", "sparse-switch payload out of range");
                    return return_value;
                }
                const uint16_t* payload = insns + pbase;
                const auto* ss =
                    reinterpret_cast<const Instruction::SparseSwitchPayload*>(payload);
                // keys are case_count int32s, then targets are case_count int32s.
                const uint64_t need =
                    static_cast<uint64_t>(pbase) + 2u +
                    static_cast<uint64_t>(ss->case_count) * 4u;
                if (need > insns_size) {
                    ThrowException("Ljava/lang/InternalError;", "sparse-switch cases out of range");
                    return return_value;
                }
                const int32_t* keys = ss->GetKeys();
                const int32_t* targets = ss->GetTargets();
                bool matched = false;
                for (uint16_t i = 0; i < ss->case_count; ++i) {
                    if (keys[i] == test) {
                        const int64_t dest = static_cast<int64_t>(base) + targets[i];
                        if (dest >= 0 && dest <= static_cast<int64_t>(insns_size)) {
                            dex_pc = static_cast<uint32_t>(dest);
                            matched = true;
                        }
                        break;
                    }
                }
                if (matched) continue;
                break;
            }

            case Instruction::FILL_ARRAY_DATA: {
                auto* arr = static_cast<DexArray*>(frame->GetRef(inst->VRegA_31t()));
                if (arr == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "fill-array-data on null");
                    return return_value;
                }
                // Payload bounds first: ident + width + count, then the bytes.
                const int32_t off = inst->VRegB_31t();
                const uint32_t base = static_cast<uint32_t>(dex_pc);
                const uint32_t pbase = base + static_cast<uint32_t>(off);
                if (off < 0 || pbase + 4 > insns_size) {
                    ThrowException("Ljava/lang/InternalError;", "fill-array-data out of range");
                    return return_value;
                }
                const uint16_t* payload = insns + pbase;
                const auto* ad = reinterpret_cast<const Instruction::ArrayDataPayload*>(payload);
                // Width must match the array's real element size: a width=8
                // payload on a byte array would copy 8x the allocation.
                const uint32_t elem_size =
                    arr->clazz != nullptr && arr->clazz->component_type != nullptr
                        ? DexClassLinker::ElementSize(arr->clazz->component_type)
                        : 0;
                if (elem_size == 0 || ad->element_width != elem_size ||
                    static_cast<int32_t>(ad->element_count) > arr->length) {
                    ThrowException("Ljava/lang/ArrayIndexOutOfBoundsException;",
                                   "fill-array-data is longer than array");
                    return return_value;
                }
                // Payload bytes must also fit: count*width bytes from data.
                const uint64_t blob =
                    static_cast<uint64_t>(ad->element_count) * ad->element_width;
                if (static_cast<uint64_t>(pbase) + 8 + blob >
                    static_cast<uint64_t>(insns_size) * 2) {
                    ThrowException("Ljava/lang/InternalError;", "fill-array-data overruns code");
                    return return_value;
                }
                std::memcpy(arr->Data(), ad->data,
                            static_cast<size_t>(ad->element_count) * ad->element_width);
                break;
            }

            // Exception.
            case Instruction::THROW: {
                DexObject* ex = frame->GetRef(inst->VRegA_11x());
                if (ex == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "throw null");
                    return return_value;
                }
                // Interpreter slot only, by design: a Java `throw` is caught
                // by FindCatchHandler in this same interpreter. Mirroring it
                // into the JNI env made a normal throw visible to a native
                // ExceptionCheck on the call stack above and hung boot. The
                // reverse gap (native throw invisible to bytecode) is covered
                // by DexJniEnv::SetPendingException, which already forwards.
                pending_exception_ = ex;
                last_error_ = std::string("throw ") +
                              (ex->clazz != nullptr ? ex->clazz->PrettyName() : "?");
                // Guest `throw` skips ThrowException(), so the trace has to be
                // captured here too or an app-thrown exception would arrive with no
                // location at all.
                pending_exception_trace_ = BuildStackTrace();
                return return_value;
            }

            // There are no real threads so the monitor only counts; enough for single-thread code
            // and don't make mistakes, the code is correct.
            case Instruction::MONITOR_ENTER: {
                DexObject* obj = frame->GetRef(inst->VRegA_11x());
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "monitor-enter on null");
                    return return_value;
                }
                Monitor::Enter(obj);
                break;
            }
            case Instruction::MONITOR_EXIT: {
                DexObject* obj = frame->GetRef(inst->VRegA_11x());
                if (obj == nullptr) {
                    ThrowException("Ljava/lang/NullPointerException;", "monitor-exit on null");
                    return return_value;
                }
                if (!Monitor::Exit(obj)) {
                    ThrowException("Ljava/lang/IllegalMonitorStateException;",
                                   "monitor-exit without owning the monitor");
                    return return_value;
                }
                break;
            }

            default:
                ThrowException("Ljava/lang/UnsupportedOperationException;",
                               std::string("opcode not yet implemented: ") + inst->Name());
                return return_value;
        }
        if (HasPendingException()) return return_value;
        dex_pc = next_pc;
    }

    // Falling out of the end of the method without returning - bytecode error.
    ThrowException("Ljava/lang/VerifyError;", "run out of bytecode without return");
    return return_value;
}
