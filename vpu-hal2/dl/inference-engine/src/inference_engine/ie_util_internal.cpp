// Copyright (c) 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "ie_util_internal.hpp"
#include "graph_tools.hpp"
#include "caseless.hpp"
#include "ie_utils.hpp"

#include <ie_layers.h>

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <deque>
#include <string>
#include <cassert>
#include <memory>
#include <utility>
#include <iomanip>

namespace InferenceEngine {

namespace {
template<typename Visitor>
void groupSubgraphsHelper(const InferenceEngine::CNNLayerPtr& layer,
                          Visitor&& visitor) {
    for (auto&& out : layer->outData) {
        for (auto&& out_link : out->getInputTo()) {
            auto& nextLayer = out_link.second;
            if (nullptr != nextLayer &&
                visitor(layer, nextLayer)) {
                groupSubgraphsHelper(nextLayer, std::forward<Visitor>(visitor));
            }
        }
    }
}
}  // namespace

std::vector<std::vector<CNNLayerPtr> >
groupSubgraphs(ICNNNetwork& network,
               std::function<bool(const CNNLayerPtr&,
                                  const CNNLayerPtr&)> splitter) {
    // TODO splitter std::function is heavy and can be replaced with
    // llvm::function_ref-like lightweight callable when we add one
    std::unordered_set<InferenceEngine::CNNLayerPtr> visitedObjects;
    std::deque<InferenceEngine::CNNLayerPtr> layersToCheck;
    InputsDataMap inputs;
    network.getInputsInfo(inputs);
    for (auto&& input : inputs) {
        auto data = input.second->getInputData();
        for (auto&& to : data->getInputTo()) {
            auto nextLayer = to.second;
            assert(nullptr != nextLayer);
            layersToCheck.push_front(nextLayer);
        }
    }

    std::vector<std::vector<InferenceEngine::CNNLayerPtr>> ret;

    while (!layersToCheck.empty()) {
        auto layer = layersToCheck.back();
        layersToCheck.pop_back();
        if (visitedObjects.find(layer) == visitedObjects.end()) {
            visitedObjects.insert(layer);
            std::vector<InferenceEngine::CNNLayerPtr> subgraph;
            subgraph.push_back(layer);
            groupSubgraphsHelper(layer,
                                 [&](const InferenceEngine::CNNLayerPtr& layer1,
                                     const InferenceEngine::CNNLayerPtr& layer2) {
                if (visitedObjects.find(layer2) == visitedObjects.end()) {
                    if (splitter(layer1, layer2)) {
                        // Layer belongs to different subgraph
                        // Do not add it to visited objects list here,
                        // because we need to visit it during next while iteration
                        layersToCheck.push_front(layer2);
                        return false;
                    } else {
                        // Layer belongs to same subgraph
                        // add it to list
                        subgraph.push_back(layer2);
                        visitedObjects.insert(layer2);
                        return true;
                    }
                }
                return false;
            });
            ret.emplace_back(std::move(subgraph));
        }
    }

    return ret;
}


DataPtr cloneData(const InferenceEngine::Data& source) {
    auto cloned = std::make_shared<InferenceEngine::Data>(source);
    cloned->getCreatorLayer().reset();
    cloned->getInputTo().clear();
    return cloned;
}

namespace {
template<typename T>
CNNLayerPtr layerCloneImpl(const CNNLayer* source) {
    auto layer = dynamic_cast<const T*>(source);
    if (nullptr != layer) {
        auto newLayer = std::make_shared<T>(*layer);
        newLayer->_fusedWith = nullptr;
        newLayer->outData.clear();
        newLayer->insData.clear();
        return std::static_pointer_cast<CNNLayer>(newLayer);
    }
    return nullptr;
}

}  // namespace

CNNLayerPtr clonelayer(const CNNLayer& source) {
    using fptr = CNNLayerPtr (*)(const CNNLayer*);
    // Most derived layers must go first in this list
    static const fptr cloners[] = {
        &layerCloneImpl<BatchNormalizationLayer>,
        &layerCloneImpl<PowerLayer             >,
        &layerCloneImpl<ScaleShiftLayer        >,
        &layerCloneImpl<TileLayer              >,
        &layerCloneImpl<ReshapeLayer           >,
        &layerCloneImpl<CropLayer              >,
        &layerCloneImpl<EltwiseLayer           >,
        &layerCloneImpl<ClampLayer             >,
        &layerCloneImpl<ReLULayer              >,
#ifdef AKS
        &layerCloneImpl<TanHLayer              >,
        &layerCloneImpl<SigmoidLayer           >,
#endif
        &layerCloneImpl<SoftMaxLayer           >,
        &layerCloneImpl<NormLayer              >,
        &layerCloneImpl<SplitLayer             >,
        &layerCloneImpl<ConcatLayer            >,
        &layerCloneImpl<FullyConnectedLayer    >,
        &layerCloneImpl<PoolingLayer           >,
        &layerCloneImpl<DeconvolutionLayer     >,
        &layerCloneImpl<ConvolutionLayer       >,
        &layerCloneImpl<WeightableLayer        >,
        &layerCloneImpl<CNNLayer               >
    };
    for (auto cloner : cloners) {
        auto cloned = cloner(&source);
        if (nullptr != cloned) {
            return cloned;
        }
    }
    assert(!"All layers derived from CNNLayer so we must never get here");
    return nullptr;  // Silence "control may reach end of non-void function" warning
}

details::CNNNetworkImplPtr cloneNet(const ICNNNetwork &network) {
    std::vector<CNNLayerPtr> layers;
    details::CNNNetworkIterator i(const_cast<ICNNNetwork *>(&network));
    while (i != details::CNNNetworkIterator()) {
        layers.push_back(*i);
        i++;
    }
    // copy of the network
    details::CNNNetworkImplPtr net = cloneNet(layers);
    // going over output layers and duplicatig them:
    OutputsDataMap outputs;
    network.getOutputsInfo(outputs);
    for (auto o : outputs) {
        net->addOutput(o.first);
    }

    return net;
}


details::CNNNetworkImplPtr cloneNet(const std::vector<CNNLayerPtr>& layers,
                                    std::function<CNNLayerPtr(const CNNLayer&)> layerCloner) {
    // TODO layerCloner std::function is heavy and can be replaced with
    // llvm::function_ref-like lightweight callable when we add one
    auto net = std::make_shared<InferenceEngine::details::CNNNetworkImpl>();

    // Src to cloned data map
    std::unordered_map<InferenceEngine::DataPtr, InferenceEngine::DataPtr> dataMap;
    std::vector<InferenceEngine::DataPtr> clonedDatas;

    auto createDataImpl = [&](const InferenceEngine::DataPtr& data) {
        assert(nullptr != data);
        if (!contains(dataMap, data)) {
            auto clonedData = cloneData(*data);
            dataMap[data] = clonedData;
            clonedDatas.push_back(clonedData);
            net->getData(clonedData->getName()) = clonedData;
            return clonedData;
        }
        return dataMap[data];
    };

    for (auto&& srcLayer : layers) {
        CNNLayerPtr clonedLayer = layerCloner(*srcLayer);
        clonedLayer->_fusedWith = nullptr;
        // We will need to reconstruct all connections in new graph
        clonedLayer->outData.clear();
        clonedLayer->insData.clear();
        net->addLayer(clonedLayer);
        for (auto&& src : srcLayer->insData) {
            auto data = src.lock();
            auto clonedData = createDataImpl(data);

            std::string inputName;
            // Find input name
            for (auto&& inp : data->getInputTo()) {
                if (srcLayer == inp.second) {
                    inputName = inp.first;
                    break;
                }
            }
            assert(!inputName.empty());
            clonedData->getInputTo().insert({ inputName, clonedLayer });
            clonedLayer->insData.push_back(clonedData);
        }

        for (auto&& data : srcLayer->outData) {
            auto clonedData = createDataImpl(data);
            clonedData->getCreatorLayer() = clonedLayer;
            clonedLayer->outData.push_back(clonedData);
            for (auto&& inp : data->getInputTo()) {
                auto layer = inp.second;
                // TODO(amalyshe) is it the best place to check priorbox and remove
                // such edge from outputs?
                if (std::find(layers.begin(), layers.end(), layer) == layers.end() &&
                    !(CaselessEq<std::string>()(layer->type, "priorbox") ||
                      CaselessEq<std::string>()(layer->type, "PriorBoxClustered"))) {
                    net->addOutput(data->getName());
                    break;
                }
            }
        }
    }

    for (auto&& data : clonedDatas) {
        auto layer = data->getCreatorLayer().lock();
        if (nullptr == layer || CaselessEq<std::string>()(layer->type, "input")) {
            auto input = std::make_shared<InferenceEngine::InputInfo>();
            input->setInputData(data);
            net->setInputInfo(input);
        }
    }

    net->resolveOutput();

    return net;
}

namespace traverse {

void forward(const CNNLayerPtr& layer, std::deque<InferenceEngine::CNNLayerPtr>& layers) {
    for (const auto& out : layer->outData) {
        for (const auto& out_link : out->getInputTo()) {
            const auto& nextLayer = out_link.second;
            if (nullptr != nextLayer) {
                layers.emplace_back(nextLayer);
            }
        }
    }
}

void backward(const CNNLayerPtr& layer, std::deque<InferenceEngine::CNNLayerPtr>& layers) {
    for (const auto& data : layer->insData) {
        const auto data_ptr = data.lock();
        const auto creatorLayer = data_ptr->creatorLayer.lock();
        if (nullptr != creatorLayer &&
            creatorLayer->type != "Input" &&
            creatorLayer->type != "input" ) {
            layers.emplace_back(creatorLayer);
        }
    }
}

void traverse(InferenceEngine::ICNNNetwork& network,
              std::function<void(InferenceEngine::CNNLayerPtr& layer)> apply,
              std::function<void(const InferenceEngine::CNNLayerPtr& layer, std::deque<InferenceEngine::CNNLayerPtr>& layers)> expand) {
    std::vector<InferenceEngine::CNNLayerPtr> layers;

    InferenceEngine::InputsDataMap inputs;
    network.getInputsInfo(inputs);
    for (const auto& input : inputs) {
        const auto data = input.second->getInputData();
        for (const auto& to : data->getInputTo()) {
            const auto nextLayer = to.second;
            assert(nullptr != nextLayer);
            layers.emplace_back(nextLayer);
        }
    }

    traverse(layers, apply, expand);
}

}  // namespace traverse


struct NodePrinter {
    enum FILL_COLOR { DATA, SUPPORTED_LAYER, UNSOPPORTED_LAYER };

    std::unordered_set<InferenceEngine::Data*> printed_data;
    std::unordered_set<InferenceEngine::CNNLayer*> printed_layers;
    std::ostream &out;

    printer_callback layer_cb;

    explicit NodePrinter(std::ostream &os, printer_callback cb)
        : out(os), layer_cb(std::move(cb)) {}

    bool isPrinted(const CNNLayerPtr &layer) {
        return static_cast<bool>(printed_layers.count(layer.get()));
    }

    bool isPrinted(const DataPtr &datum) {
        return static_cast<bool>(printed_data.count(datum.get()));
    }

    std::string colorToStr(FILL_COLOR color) {
        switch (color) {
            case DATA :
                return "#FCF6E3";
            case SUPPORTED_LAYER:
                return "#D9EAD3";
            case UNSOPPORTED_LAYER:
                return "#F4CCCC";
            default:
                return "#FFFFFF";
        }
    }

    std::string formatSize_(unsigned int x, unsigned int y) {
        return x == y ? std::to_string(x) :
               std::to_string(x) + "x" + std::to_string(y);
    }

    std::string cleanNodeName_(std::string node_name) const {
        // remove dot sumbol form node name. It is incorrectly displayed in xdot
        node_name.erase(remove(node_name.begin(), node_name.end(), '.'), node_name.end());
        return node_name;
    }

    void printLayerNode(const CNNLayerPtr &layer) {
        auto node_name = "layer_" + cleanNodeName_(layer->name);
        printed_layers.insert(layer.get());

        ordered_properties printed_properties;

        ordered_properties node_properties = {
            {"shape", "box"},
            {"style", "filled"},
            {"fillcolor", colorToStr(SUPPORTED_LAYER)}
        };

        auto type = layer->type;
        printed_properties.emplace_back("type", type);

        if (type == "Convolution") {
            auto* conv = dynamic_cast<ConvolutionLayer*>(layer.get());

            unsigned ker_y = conv->_kernel_y,
                ker_x = conv->_kernel_x,
                pad_x = conv->_padding_x,
                pad_y = conv->_padding_y,
                depth = conv->_out_depth,
                stride_x = conv->_stride_x,
                stride_y = conv->_stride_y;

            printed_properties.emplace_back("kernel size", formatSize_(ker_x, ker_y));
            printed_properties.emplace_back("output depth", std::to_string(depth));
            printed_properties.emplace_back("padding", formatSize_(pad_x, pad_y));
            printed_properties.emplace_back("stride", formatSize_(stride_x, stride_y));
        } else if (type == "Pooling") {
            auto* pool = dynamic_cast<PoolingLayer*>(layer.get());

            unsigned int ker_y = pool->_kernel_y,
                ker_x = pool->_kernel_x,
                pad_x = pool->_padding_x,
                pad_y = pool->_padding_y,
                stride_x = pool->_stride_x,
                stride_y = pool->_stride_y;

            printed_properties.emplace_back("window size", formatSize_(ker_x, ker_y));
            printed_properties.emplace_back("padding", formatSize_(pad_x, pad_y));
            printed_properties.emplace_back("stride", formatSize_(stride_x, stride_y));
        }

        if (layer_cb != nullptr) {
            layer_cb(layer, printed_properties, node_properties);
        }

        printNode(node_name, layer->name, node_properties, printed_properties);
    }

    void printDataNode(const std::shared_ptr<Data> &data) {
        auto node_name = "data_" + cleanNodeName_(data->getName());
        printed_data.insert(data.get());

        ordered_properties printed_properties;
        ordered_properties node_properties = {
            {"shape", "ellipse"},
            {"style", "filled"},
            {"fillcolor", colorToStr(DATA)}
        };

        std::stringstream dims_ss;
        size_t idx = data->dims.size();
        dims_ss << '[';
        for (auto &dim : data->dims) {
            dims_ss << dim << ((--idx) != 0u ? ", " : "");
        }
        dims_ss << ']';

        printed_properties.emplace_back("dims", dims_ss.str());
        printNode(node_name, data->name, node_properties, printed_properties);
    }

    void printNode(std::string const &node_name, const std::string &node_title,
                   ordered_properties const &node_properties,
                   ordered_properties const &printed_properties) {
        // normalization of names, removing all prohinited symbols like "/"
        std::string nodeNameN = node_name;
        std::replace(nodeNameN.begin(), nodeNameN.end(), '/', '_');
        std::string dataNameN = node_title;
        std::replace(dataNameN.begin(), dataNameN.end(), '/', '_');

        out << '\t' << nodeNameN << " [";
        for (auto &node_propertie : node_properties) {
            out << node_propertie.first << "=\"" << node_propertie.second << "\", ";
        }

        out << "label=\"" << node_title;
        for (auto &printed_propertie : printed_properties) {
            out << "\\n" << printed_propertie.first << ": " << printed_propertie.second;
        }
        out << "\"];\n";
    }

    void printEdge(const CNNLayerPtr &from_, const DataPtr &to_, bool reverse) {
        auto from_name = "layer_" + cleanNodeName_(from_->name);
        auto to_name = "data_" + cleanNodeName_(to_->getName());
        std::replace(from_name.begin(), from_name.end(), '/', '_');
        std::replace(to_name.begin(), to_name.end(), '/', '_');
        if (reverse)
            std::swap(from_name, to_name);
        out << '\t' << from_name << " -> " << to_name << ";\n";
    }
};

void saveGraphToDot(InferenceEngine::ICNNNetwork &network, std::ostream &out, printer_callback layer_cb) {
    NodePrinter printer(out, std::move(layer_cb));

    InferenceEngine::InputsDataMap networkInputs;
    network.getInputsInfo(networkInputs);
    if (networkInputs.empty()) {
        THROW_IE_EXCEPTION << "No inputs detected.";
    }

    // Get all network inputs
    CNNLayerSet inputs;
    for (auto input : networkInputs) {
        for (auto l : input.second->getInputData()->inputTo) {
            inputs.insert(l.second);
        }
    }

    out << "strict digraph Network {\n";
    // Traverse graph and print nodes
    CNNNetForestDFS(inputs, [&](CNNLayerPtr layer) {
        printer.printLayerNode(layer);

        // Print output Data Object
        for (auto &dataptr : layer->outData) {
            if (!printer.isPrinted(dataptr)) {
                printer.printDataNode(dataptr);
            }
            printer.printEdge(layer, dataptr, false);
        }

        // Print input Data objects
        for (auto &datum : layer->insData) {
            auto dataptr = datum.lock();
            if (!printer.isPrinted(dataptr)) {
                printer.printDataNode(dataptr);
            }
            // in order not to keep additional set with
            // printed edges, strict keyword for digraph is used
            // to remove duplicate edges
            printer.printEdge(layer, dataptr, true);
        }
    }, true);

    out << "}" << std::endl;
}

}  // namespace InferenceEngine
