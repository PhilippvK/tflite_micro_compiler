/* Copyright 2020 Christof Petig. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/micro/kernels/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/simple_memory_allocator.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
#include "tensorflow/lite/c/builtin_op_data.h"
#include <iostream>

#define DECLARE_FUNC_SET(X) \
	namespace X { \
		void* Init(TfLiteContext* context, const char* buffer, size_t length); \
		TfLiteStatus Prepare(TfLiteContext* context, TfLiteNode* node); \
		TfLiteStatus Eval(TfLiteContext* context, TfLiteNode* node); \
	}
#define DECLARE_PREP_EVAL(X,EVAL_PREFIX) \
	namespace X { \
		TfLiteStatus EVAL_PREFIX##Prepare(TfLiteContext* context, TfLiteNode* node); \
		TfLiteStatus EVAL_PREFIX##Eval(TfLiteContext* context, TfLiteNode* node); \
		}
// too many declarations never hurt.
#define DECLARE_EVAL(X,EVAL_PREFIX) DECLARE_PREP_EVAL(X,EVAL_PREFIX)

extern TfLiteStatus FullyConnectedEval(TfLiteContext* context, TfLiteNode* node);
namespace tflite {
	namespace ops {
		namespace micro {
			DECLARE_FUNC_SET(fully_connected)
			DECLARE_FUNC_SET(conv)
			DECLARE_FUNC_SET(depthwise_conv)
			DECLARE_FUNC_SET(reshape)
			DECLARE_FUNC_SET(quantize)
			DECLARE_FUNC_SET(dequantize)
			DECLARE_PREP_EVAL(activations, Softmax)
			DECLARE_EVAL(pooling, Average)
			DECLARE_EVAL(pooling, Max)
		}
	}
}

//static std::string stage0;

static bool mem_in(void const*ptr, void const* start, void const* end){
	return ptr >= start && ptr < end;
}

#define NAME(X) case X: return #X

static std::string to_string(TfLiteType t){
	switch (t) {
	NAME(kTfLiteFloat32);
	NAME(kTfLiteInt32);
	NAME(kTfLiteUInt8);
	NAME(kTfLiteInt64);
	NAME(kTfLiteString);
	NAME(kTfLiteBool);
	NAME(kTfLiteInt16);
	NAME(kTfLiteComplex64);
	NAME(kTfLiteInt8);
	NAME(kTfLiteFloat16);
	NAME(kTfLiteFloat64);
	default: return "TfLiteType(" + std::to_string((int)t) + ")";
	}
}
static std::string to_string(TfLiteAllocationType t){
	switch (t) {
	NAME(kTfLiteMmapRo);
	NAME(kTfLiteArenaRw);
	default: return "TfLiteAllocationType(" + std::to_string((int)t) + ")";
	}
}
static std::string to_string(TfLiteFusedActivation t){
	switch (t) {
	NAME(kTfLiteActNone);
	NAME(kTfLiteActRelu);
	NAME(kTfLiteActRelu1);
	NAME(kTfLiteActRelu6);
	NAME(kTfLiteActTanh);
	NAME(kTfLiteActSignBit);
	NAME(kTfLiteActSigmoid);
	default: return "TfLiteFusedActivation(" + std::to_string((int)t) + ")";
	}
}
static std::string to_string(TfLiteFullyConnectedWeightsFormat t){
	switch (t) {
	NAME(kTfLiteFullyConnectedWeightsFormatDefault);
	NAME(kTfLiteFullyConnectedWeightsFormatShuffled4x16Int8);
	default: return "TfLiteFullyConnectedWeightsFormat(" + std::to_string((int)t) + ")";
	}
}
static std::string to_string(bool t){
	switch (t) {
	NAME(false);
	NAME(true);
//	default: return "bool(" + std::to_string((int)t) + ")";
	}
}
static std::string to_string(TfLitePadding t){
	switch (t) {
	NAME(kTfLitePaddingUnknown);
	NAME(kTfLitePaddingSame);
	NAME(kTfLitePaddingValid);
	default: return "TfLitePadding(" + std::to_string((int)t) + ")";
	}
}

static std::string to_string(TfLitePaddingValues const& v){
	return std::string("{ ") + std::to_string(v.width) + ","
		+ std::to_string(v.height) + ", "
		+ std::to_string(v.width_offset) + ","
		+ std::to_string(v.height_offset) + " }";
}

// implementation from https://github.com/tum-ei-eda/tflm-offline-interpreter/blob/master/src/main.cpp
// Tracks the last allocation size.
class AllocatorToGetLastAllocSize : public tflite::BuiltinDataAllocator {
 public:
  void *Allocate(size_t size, size_t alignment_hint) override {
    lastAllocSize = size;
    return malloc(size);
  }
  void Deallocate(void *data) override { free(data); }
  size_t GetLastAllocSize() { return lastAllocSize; }

 private:
  size_t lastAllocSize = 0;
};
size_t GetBuiltinDataSize(tflite::BuiltinOperator opType,
                          const tflite::SubGraph *subgraph) {
#if 1
  // There seems to be no simple query function for this, so tickle the
  // information out of the parse function.
  auto dummyOp = subgraph->operators()->Get(0);
  tflite::MicroErrorReporter errReporter;
  AllocatorToGetLastAllocSize allocator;
  void *outData = nullptr;
  if (tflite::ParseOpData(dummyOp, opType, &errReporter, &allocator, &outData)==kTfLiteOk)
	  free(outData);
  return allocator.GetLastAllocSize();
#else
	return 8;
#endif
}
// end of inserted implementation

// we need to autogenerate this!
static void dump_builtin(tflite::BuiltinOperator op, void const* data, std::string const& name,
				const tflite::SubGraph *subgraph) {
	std::cout << "static const ";
	switch (op) {
	case tflite::BuiltinOperator_CONV_2D: {
		std::cout << "TfLiteConvParams " << name << " = { ";
		TfLiteConvParams const*p = (TfLiteConvParams const*)data;
		std::cout << to_string(p->padding) << ", "
			<< std::to_string(p->stride_width) << ","
			<< std::to_string(p->stride_height) << ", "

			<< to_string(p->activation) << ", "
			<< std::to_string(p->dilation_width_factor) << ","
			<< std::to_string(p->dilation_height_factor) << " };"
			;
	}
	break;
	case tflite::BuiltinOperator_DEPTHWISE_CONV_2D: {
		std::cout << "TfLiteDepthwiseConvParams " << name << " = { ";
		TfLiteDepthwiseConvParams const*p = (TfLiteDepthwiseConvParams const*)data;
		std::cout << to_string(p->padding) << ", "
			<< std::to_string(p->stride_width) << ","
			<< std::to_string(p->stride_height) << ", "

			<< std::to_string(p->depth_multiplier) << ", "
			<< to_string(p->activation) << ", "
			<< std::to_string(p->dilation_width_factor) << ","
			<< std::to_string(p->dilation_height_factor) << " };";
	}
	break;
	case tflite::BuiltinOperator_FULLY_CONNECTED: {
		std::cout << "TfLiteFullyConnectedParams " << name << " = { ";
		TfLiteFullyConnectedParams const*p = (TfLiteFullyConnectedParams const*)data;
		std::cout << to_string(p->activation) << ", "
			<< to_string(p->weights_format) << ", "
			<< to_string(p->keep_num_dims) << ", "
			<< to_string(p->asymmetric_quantize_inputs) << " };";
	}
	break;
	case tflite::BuiltinOperator_AVERAGE_POOL_2D: {
		std::cout << "TfLitePoolParams " << name << " = { ";
		TfLitePoolParams const*p = (TfLitePoolParams const*)data;
		std::cout << to_string(p->padding) << ", "
			<< std::to_string(p->stride_width) << ","
			<< std::to_string(p->stride_height) << ", "
			<< std::to_string(p->filter_width) << ","
			<< std::to_string(p->filter_height) << ", "
			<< to_string(p->activation) << ", { "
			<< to_string(p->computed.padding) << " } };";
	}
	break;
	// case tflite::BuiltinOperator_RESHAPE: {
	//  	std::cout << "uint8_t " << name << " = 0; /* is there reshape data? */";
	// }
	// break;
	case tflite::BuiltinOperator_SOFTMAX: {
		std::cout << "TfLiteSoftmaxParams " << name << " = { ";
		TfLiteSoftmaxParams const*p = (TfLiteSoftmaxParams const*)data;
		std::cout << std::to_string(p->beta) << " };";
	}
	break;
	default: {
		size_t datalen= GetBuiltinDataSize(op, subgraph);
		std::cout << "uint8_t " << name << "["<<datalen<<"] = { ";
		for (uint32_t i=0; i<datalen; ++i)
			std::cout << int(((uint8_t const*)data)[i]) << ", ";
		std::cout <<  " }; /* op type "
		<< int(op) << " */";
	}
		break;
	}
	std::cout << std::endl;
}

#define FUNCNAME(X) if (ptr==&X) return std::make_pair<std::string,bool>(#X,true)
#define FUNC_SET(X) FUNCNAME(tflite::ops::micro::X::Init); FUNCNAME(tflite::ops::micro::X::Prepare); FUNCNAME(tflite::ops::micro::X::Eval)
#define FUNC_SET2(X) FUNCNAME(tflite::ops::micro::X::Prepare); FUNCNAME(tflite::ops::micro::X::Eval)
#define FUNC_SET2P(X,Y) FUNCNAME(tflite::ops::micro::X::Y##Prepare); FUNCNAME(tflite::ops::micro::X::Y##Eval)

static std::pair<std::string,bool> function_name(void const*ptr) {
#if 0
	FUNCNAME(FullyConnectedEval);
#endif
	FUNC_SET(fully_connected);
	FUNC_SET(conv);
	FUNC_SET(depthwise_conv);
	FUNC_SET(dequantize);
	FUNC_SET(quantize);
	FUNC_SET2(reshape);
	FUNC_SET2P(activations, Softmax);
	FUNCNAME(tflite::ops::micro::pooling::MaxEval);
	FUNCNAME(tflite::ops::micro::pooling::AverageEval);
	return std::make_pair<std::string,bool>("unknown_function", false);
}

// outside declarations are written later and init_statements inside the init function
static void declare_function(const void* ptr, tflite::BuiltinOperator op,
				std::set<std::string> &known, std::set<tflite::BuiltinOperator> &known2,
				char const* result, char const* arguments, 
				std::string& outside_declarations, std::string& init_statements) {
	if (ptr) {
		std::pair<std::string,bool> name = function_name(ptr);
		if (!name.second) {
			if (known2.find(op)==known2.end()) {
				std::string opname = tflite::EnumNameBuiltinOperator(op);
				std::cout << "extern TfLiteRegistration *Register_" << opname << "(void);\n";

				outside_declarations += "static TfLiteRegistration *operator_" + opname + ";\n";
				init_statements += "  operator_"+opname
					+ " = tflite::ops::micro::Register_" + opname + "();\n";
				known2.insert(op);
			}
		}
		else if (known.find(name.first)==known.end()) {
			if (name.first.substr(0,20)=="tflite::ops::micro::") {
				std::string::size_type sep = name.first.find("::", 20);
				if (sep != std::string::npos) {
					std::string nmspc = name.first.substr(20, sep - 20);
					std::string fun = name.first.substr(sep + 2);
					std::cout << "namespace " << nmspc << " { extern " << result << ' ' << fun
						<< '(' << arguments << "); }" << std::endl;
				}
			}
			else { // hopefully outside of namespace declaration fits here
				outside_declarations += "extern " + std::string(result) + ' ' + name.first + '(' + arguments + ");\n";
			}
			known.insert(name.first);
		}
	}
}

void dump_data(char const* prefix, tflite::MicroInterpreter *interpreter, 
	uint8_t const*tflite_array, uint8_t const* tflite_end, 
	uint8_t const*tensor_arena, uint8_t const* arena_end) {
	tflite::Model const* model = ::tflite::GetModel(tflite_array);  // needed for size calculation
	std::cout << "#include \"tensorflow/lite/c/builtin_op_data.h\"" << std::endl;
	std::cout << std::endl;

	// declare functions
	std::cout << "namespace tflite { namespace ops { namespace micro {" << std::endl;
	std::set<std::string> known;
	std::set<tflite::BuiltinOperator> known2;
	std::string outside_declarations, init_statements;
	for (uint32_t i = 0; i < interpreter->operators_size(); ++i) {
		declare_function((const void*)(interpreter->node_and_registration(i).registration->init), 
			tflite::BuiltinOperator(interpreter->node_and_registration(i).registration->builtin_code), 
			known, known2, "void*", "TfLiteContext*, const char*, size_t", outside_declarations, init_statements);
		declare_function((const void*)(interpreter->node_and_registration(i).registration->prepare), 
			tflite::BuiltinOperator(interpreter->node_and_registration(i).registration->builtin_code), 
			known, known2, "TfLiteStatus", "TfLiteContext*, TfLiteNode*", outside_declarations, init_statements);
		declare_function((const void*)(interpreter->node_and_registration(i).registration->invoke), 
			tflite::BuiltinOperator(interpreter->node_and_registration(i).registration->builtin_code), 
			known, known2, "TfLiteStatus", "TfLiteContext*, TfLiteNode*", outside_declarations, init_statements);
	}
	std::cout << "} } }" << std::endl;
	std::cout << outside_declarations;
	std::cout << std::endl;

	// create static tensor+node+context storage
	std::cout << "static TfLiteTensor " << prefix << "tensors[" << interpreter->tensors_size() << "];" << std::endl;
	std::cout << "static TfLiteNode " << prefix << "nodes[" << interpreter->operators_size() << "];" << std::endl;
	std::cout << "static TfLiteContext " << prefix << "context;" << std::endl;
	for (uint32_t i = 0; i < interpreter->operators_size(); ++i) {
		if (mem_in(interpreter->node_and_registration(i).node.builtin_data, tensor_arena, arena_end)) {
			dump_builtin(tflite::BuiltinOperator(interpreter->node_and_registration(i).registration->builtin_code), 
				interpreter->node_and_registration(i).node.builtin_data, 
				std::string(prefix) + "opdata" + std::to_string(i),
				model->subgraphs()[0][0]);
		}
	}
	// quantization parameters
	for (uint32_t i = 0; i < interpreter->tensors_size(); ++i) {
		TfLiteTensor const* t = interpreter->tensor(i);
		if (t->quantization.type == kTfLiteAffineQuantization) {
			TfLiteAffineQuantization const* q = (TfLiteAffineQuantization const*)t->quantization.params;
			std::cout << "static const struct { int sz; float elem[" << q->scale->size 
				<< "]; } " << prefix << "quant_scale" << i << " = { "
				<< q->scale->size << ", { ";
			for (uint32_t j=0;j<q->scale->size ; ++j){
				std::cout << q->scale->data[j] << ", ";
			}
			std::cout << "} };" << std::endl;
			std::cout << "static const int " << prefix << "quant_zero" << i 
				<< "[" << q->zero_point->size+1 << "] = { " 
				<< q->zero_point->size << ", ";
			for (uint32_t j=0;j<q->zero_point->size ; ++j){
				std::cout << q->zero_point->data[j] << ", ";
			}
			std::cout << "};" << std::endl;
			std::cout << "static const TfLiteAffineQuantization " << prefix << "quantization" << i << " = { "
				<< "(TfLiteFloatArray*)&" << prefix << "quant_scale" << i << ", "
				<< "(TfLiteIntArray*)&" << prefix << "quant_zero" << i << ", "
				<< q->quantized_dimension << " };" << std::endl;
		}
	}
	std::cout << std::endl;

	// allocator helpers
	std::cout << "static void* next_allocation = nullptr;" << std::endl;
	std::cout << "static TfLiteStatus AllocatePersistentBuffer(struct TfLiteContext* ctx, size_t bytes, void** ptr) {" << std::endl;
	std::cout << "  *ptr = next_allocation;" << std::endl;
	std::cout << "  next_allocation = nullptr;" << std::endl;
	std::cout << "  return kTfLiteOk;" << std::endl;
	std::cout << "}" << std::endl;
	std::cout << std::endl;

	// init function (setting up tensors+node, call Init and Prepare)
	std::cout << "void " << prefix << "init(uint8_t const*tflite_array, uint8_t const*tensor_arena) {" << std::endl;
	for (uint32_t i = 0; i < interpreter->tensors_size(); ++i) {
		TfLiteTensor const* t = interpreter->tensor(i);
		std::cout << "  " << prefix << "tensors[" << i << "].type = " << to_string(t->type) << ';' << std::endl;
		std::cout << "  " << prefix << "tensors[" << i << "].allocation_type = " << to_string(t->allocation_type) << ';' << std::endl;
		if (mem_in(t->name, tflite_array, tflite_end)) {
			std::cout << "  " << prefix << "tensors[" << i << "].name = (char*)(tflite_array + " << (((uint8_t const*)t->name) - tflite_array)
				<< "); /* " << t->name << " */" << std::endl;
		}
		else {
			std::cout << "  " << prefix << "tensors[" << i << "].name = (char*)\"" << t->name << "\";" << std::endl;
		}
		if (mem_in(t->dims, tflite_array, tflite_end)) {
			std::cout << "  " << prefix << "tensors[" << i << "].dims = (struct TfLiteIntArray*)(tflite_array + " << (((uint8_t const*)t->dims) - tflite_array) << "); /* (";
			for (int32_t j = 0; j < t->dims->size; ++j) std::cout << t->dims->data[j] << ',';
			std::cout << ") */" << std::endl;
		}
		if (mem_in(t->data.raw_const, tflite_array, tflite_end))
			std::cout << "  " << prefix << "tensors[" << i << "].data.raw_const = (const char*)(tflite_array + " << (((uint8_t const*)t->data.raw_const) - tflite_array) << ");" << std::endl;
		else if (mem_in(t->data.raw_const, tensor_arena, arena_end))
			std::cout << "  " << prefix << "tensors[" << i << "].data.raw = (char*)(tensor_arena + " << (((uint8_t const*)t->data.raw_const) - tensor_arena) << ");" << std::endl;
		if (t->params.scale!=0.0f) {
			std::cout << "  " << prefix << "tensors[" << i << "].params.scale = " << t->params.scale << ";" << std::endl;
			std::cout << "  " << prefix << "tensors[" << i << "].params.zero_point = " << t->params.zero_point << ";" << std::endl;
		}
		if (t->quantization.type == kTfLiteAffineQuantization) {
			std::cout << "  " << prefix << "tensors[" << i << "].quantization.type = kTfLiteAffineQuantization;" << std::endl;
			std::cout << "  " << prefix << "tensors[" << i << "].quantization.params = (void*)&" 
				<< prefix << "quantization" << i << ";" << std::endl;
		}
	}
	for (uint32_t i = 0; i < interpreter->operators_size(); ++i) {
		if (mem_in(interpreter->node_and_registration(i).node.inputs, tflite_array, tflite_end)) {
			std::cout << "  " << prefix << "nodes[" << i << "].inputs = (struct TfLiteIntArray*)(tflite_array + " << (((uint8_t const*)interpreter->node_and_registration(i).node.inputs) - tflite_array) << "); /* (";
			for (int32_t j = 0; j < interpreter->node_and_registration(i).node.inputs->size; ++j) std::cout << interpreter->node_and_registration(i).node.inputs->data[j] << ',';
			std::cout << ") */" << std::endl;
		}
		if (mem_in(interpreter->node_and_registration(i).node.outputs, tflite_array, tflite_end)) {
			std::cout << "  " << prefix << "nodes[" << i << "].outputs = (struct TfLiteIntArray*)(tflite_array + " << (((uint8_t const*)interpreter->node_and_registration(i).node.outputs) - tflite_array) << "); /* (";
			for (int32_t j = 0; j < interpreter->node_and_registration(i).node.outputs->size; ++j) std::cout << interpreter->node_and_registration(i).node.outputs->data[j] << ',';
			std::cout << ") */" << std::endl;
		}
		if (mem_in(interpreter->node_and_registration(i).node.builtin_data, tensor_arena, arena_end)) {
			std::cout << "  " << prefix << "nodes[" << i << "].builtin_data = (void*)&" << prefix << "opdata" << i << ";" << std::endl;
		}
	}
	std::cout << "  " << prefix << "context.tensors_size = " << interpreter->tensors_size() << ";" << std::endl;
	std::cout << "  " << prefix << "context.tensors = (TfLiteTensor*)" << prefix << "tensors;" << std::endl;
	std::cout << "  " << prefix << "context.AllocatePersistentBuffer = &AllocatePersistentBuffer;" << std::endl;
	std::cout << init_statements;
	for (uint32_t i = 0; i < interpreter->operators_size(); ++i) {
		if (interpreter->node_and_registration(i).registration->init) {
			// TODO: There is a good chance that just assigning user_data will do the trick as well (unless it gets initialized)
			if (mem_in(interpreter->node_and_registration(i).node.user_data, tensor_arena, arena_end)) {
				std::cout << "  next_allocation = (void*)(tensor_arena + " << (((uint8_t const*)interpreter->node_and_registration(i).node.user_data) - tensor_arena) << ");" << std::endl;
			}
			std::pair<std::string,bool> name= function_name((const void*)(interpreter->node_and_registration(i).registration->init));
			if (!name.second) 
				name.first= "(*(operator_" 
				+ std::string(tflite::EnumNameBuiltinOperator(tflite::BuiltinOperator(interpreter->node_and_registration(i).registration->builtin_code)))
				+ "->init))";
			std::cout << "  " << prefix << "nodes[" << i << "].user_data = "
				<< name.first
				// TODO: Handle custom operators
				<< "(&" << prefix << "context, (const char*)(" << prefix << "nodes[" << i << "].builtin_data), 0);" << std::endl;
		}
	}
	for (uint32_t i = 0; i < interpreter->operators_size(); ++i) {
		if (interpreter->node_and_registration(i).registration->prepare) {
			std::pair<std::string,bool> name= function_name((const void*)(interpreter->node_and_registration(i).registration->prepare));
			if (!name.second) 
				name.first= "(*(operator_" 
				+ std::string(tflite::EnumNameBuiltinOperator(tflite::BuiltinOperator(interpreter->node_and_registration(i).registration->builtin_code)))
				+ "->prepare))";
			std::cout << "  " 
				<< name.first
				<< "(&" << prefix << "context, &" << prefix << "nodes[" << i << "]);" << std::endl;
		}
	}
	std::cout << '}' << std::endl;
	std::cout << std::endl;

	// invoke function (calling Eval)
	std::cout << "void " << prefix << "invoke(void const* (inputs[" << interpreter->inputs().size() 
		<< "]), void * (outputs[" << interpreter->outputs().size() << "])) {" << std::endl;
	for (uint32_t i = 0; i < interpreter->inputs().size(); ++i) {
		std::cout << "  " << prefix << "tensors[" << interpreter->inputs()[i] << "].data.raw_const = (const char*)(inputs[" << i << "]);" << std::endl;
	}
	for (uint32_t i = 0; i < interpreter->outputs().size(); ++i) {
		std::cout << "  " << prefix << "tensors[" << interpreter->outputs()[i] << "].data.raw = (char*)(outputs[" << i << "]);" << std::endl;
	}
	for (uint32_t i = 0; i < interpreter->operators_size(); ++i) {
		std::pair<std::string, bool> funname = function_name((const void*)(interpreter->node_and_registration(i).registration->invoke));
		if (!funname.second)
			funname.first= "(*(operator_" 
			+ std::string(tflite::EnumNameBuiltinOperator(tflite::BuiltinOperator(interpreter->node_and_registration(i).registration->builtin_code)))
			+ "->invoke))";
		std::cout << "  "
			<< funname.first
			<< "(&" << prefix << "context, &" << prefix << "nodes[" << i << "]);" << std::endl;
		// if (funname=="unknown_function") {
		// 	std::cerr << "unknown function for code " << int(interpreter->node_and_registration(i).registration->builtin_code) << std::endl;
		// }
	}
	std::cout << "}" << std::endl;
}
