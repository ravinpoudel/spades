/*
 * paths.hpp
 *
 *  Created on: Aug 3, 2011
 *      Author: andrey
 */

#ifndef PATHS_HPP_
#define PATHS_HPP_

#include <cmath>

#include "lc_common.hpp"
#include "loop.hpp"


namespace long_contigs {

using namespace debruijn_graph;

// ====== Weight functions ======
//Weight filter
double WeightFunction(double weight) {
	return math::gr(weight, 0.0) ? 1.0 : 0.0;
}

double CorrectWeightByAdvanced(double weight, double advWeight) {
	return math::gr(advWeight, 0.0) ? weight * lc_cfg::get().es.advanced_coeff * lc_cfg::get().es.priority_coeff : weight;
}



//Calculate weight
double GetWeight(omnigraph::PairedInfoIndex<Graph>::PairInfos pairs, PairedInfoIndexLibrary& pairedInfoLibrary,
		int distance, int distanceDev, bool useWeightFunction = false,
		omnigraph::PairedInfoIndex<Graph>::PairInfos* ad_pairs = 0) {
	double weight = 0;

	for (auto iter = pairs.begin(); iter != pairs.end(); ++iter) {


		int pairedDistance = rounded_d(*iter);

		if (lc_cfg::get().es.use_delta_first) {
			if (iter->variance != 0) {
				distanceDev = iter->variance;
			}
		}

		//Can be modified according to distance comparison
		if (pairedDistance >= distance - distanceDev &&
				pairedDistance <= distance + distanceDev) {
			double w = iter->weight;

			if (lc_cfg::get().es.fix_weight) {
				w = pairedInfoLibrary.NormalizeWeight(*iter);
			}

			weight += w;
		}

		if (ad_pairs) {
			for (auto aiter = ad_pairs->begin(); aiter != ad_pairs->end(); ++aiter) {
				int ad_pairedDistance = rounded_d(*aiter);
				//Can be modified according to distance comparison
				if (ad_pairedDistance >= distance - distanceDev &&
						ad_pairedDistance <= distance + distanceDev) {

					weight = CorrectWeightByAdvanced(weight, aiter->weight);
				}
			}
		}
	}




	return useWeightFunction ? WeightFunction(weight) : weight;
}

//Weight fixing coefficient, sum weight of ideal info
int FixingCoefficient(Graph& g, const BidirectionalPath& path, EdgeId edge, size_t edgesToExclude, PairedInfoIndexLibrary& pairedInfoLibrary, bool forward) {
	int pathLen = 0;
	size_t start = forward ? 0 : edgesToExclude;
	size_t end = forward ? path.size() - edgesToExclude : path.size();
	for (size_t i = start; i < end; ++i) {
		pathLen += g.length(path[i]);
	}
	int exclLen = PathLength(g, path) - pathLen;
	int edgeLen = g.length(edge);

	int is = pairedInfoLibrary.insertSize;
	int rs = pairedInfoLibrary.readSize;

	int right = std::min(is, exclLen + edgeLen + rs);
	int left = std::max(exclLen - is, - rs - pathLen) + is;

	int delta = right - left + 1 - K + pairedInfoLibrary.is_delta;

	return delta; // > 0 ? delta : -1;
}

//Fixing weight value
double WeightFixing(Graph& g, const BidirectionalPath& path, EdgeId edge, size_t edgesToExclude, PairedInfoIndexLibrary& pairedInfoLibrary, double weight, bool forward) {
	int coeff = FixingCoefficient(g, path, edge, edgesToExclude, pairedInfoLibrary, forward);
	if (coeff < 0 && weight != 0) {
		INFO("Strange fixing!!! Weight: " << weight << ", c = " << coeff << ", edge: " << edge << " = " << g.length(edge));
		PrintPath(g ,path);
		return 0;
	}

	return weight/(double) coeff;
}

//Calculate weight for particular path extension from one library
double ExtentionWeight(Graph& g, BidirectionalPath& path, PathLengths& lengths, EdgeId e, PairedInfoIndexLibrary& pairedInfoLibrary,
		size_t edgesToExclude, bool forward, bool useWeightFunction = false, size_t additionalGapLength = 0) {
	double weight = 0;
	int edgeLength = forward ? 0 : g.length(e);
	size_t start = forward ? 0 : edgesToExclude;
	size_t end = forward ? path.size() - edgesToExclude : path.size();

	static int DISTANCE_DEV = cfg::get().etalon_info_mode ? lc_cfg::get().es.etalon_distance_dev : pairedInfoLibrary.var;

	for(size_t i = start; i < end; ++i) {
		EdgeId edge = path[i];
		omnigraph::PairedInfoIndex<Graph>::PairInfos pairs =
				forward ? pairedInfoLibrary.pairedInfoIndex->GetEdgePairInfo(edge, e) : pairedInfoLibrary.pairedInfoIndex->GetEdgePairInfo(e, edge);
		int distance = lengths[i] + edgeLength + additionalGapLength;

		double w = 0;
		if (pairedInfoLibrary.has_advanced) {
			omnigraph::PairedInfoIndex<Graph>::PairInfos ad_pairs = forward ? pairedInfoLibrary.advanced->pairedInfoIndex->GetEdgePairInfo(edge, e) : pairedInfoLibrary.advanced->pairedInfoIndex->GetEdgePairInfo(e, edge);
			w = GetWeight(pairs, pairedInfoLibrary, distance, DISTANCE_DEV, useWeightFunction, lc_cfg::get().es.use_advanced ? &ad_pairs : 0);
		} else {
			w = GetWeight(pairs, pairedInfoLibrary, distance, DISTANCE_DEV, useWeightFunction, 0);
		}
		weight += w;
	}

	return weight;
}

//Weight from a set of libraries
double ExtentionWeight(Graph& g, BidirectionalPath& path, PathLengths& lengths, EdgeId e, PairedInfoIndices& pairedInfo,
		size_t edgesToExclude, bool forward, bool useWeightFunction = false, size_t additionalGapLength = 0) {

	double weight = 0;
	for (auto lib = pairedInfo.begin(); lib != pairedInfo.end(); ++lib) {
		weight += ExtentionWeight(g, path, lengths, e, *lib, edgesToExclude, forward, useWeightFunction, additionalGapLength);
	}
	return weight;
}

// ====== Extension functions ======

//Check whether selected extension is good enough
EdgeId ExtensionGoodEnough(EdgeId edge, double weight, double threshold) {
	//Condition of passing threshold is to be done
	return weight > threshold ? edge : 0;
}

//Check whether selected extension is good enough
EdgeId ExtensionGoodEnough(EdgeId edge, double weight, double threshold, Graph& g, BidirectionalPath& path, PathStopHandler& handler, bool forward) {
	//Condition of passing threshold is to be done
	if (weight > threshold) {
		return edge;
	} else {
		handler.AddStop(&path, WEAK_EXTENSION, forward);
		return 0;
	}
}



void FindEdges(Graph& g, EdgeId edge, int depth, std::vector<EdgeId>& result, std::vector<int>& distances, bool forward) {
	std::vector<int> depths;
	result.clear();
	distances.clear();
	int i = 0;

	result.push_back(edge);
	depths.push_back(i);
	distances.push_back(0);

	while (i < depth) {
		int j = result.size() - 1;
		while (j >= 0 && depths[j] == i) {
			auto edges = forward ? g.OutgoingEdges(g.EdgeEnd(result[j])) : g.IncomingEdges(g.EdgeStart(result[j]));
			int len = g.length(result[j]);

			for (auto iter = edges.begin(); iter != edges.end(); ++iter) {
				result.push_back(*iter);
				depths.push_back(i + 1);

				distances.push_back(distances[j] + len);
			}
			--j;
		}
		++i;
	}
	INFO("== Depth info == ");
	PrintPath(g, result);
	for (int i = 0; i < (int) result.size(); ++i) {
		INFO("D = " << distances[i] << ", DEPTH = " << depths[i]);
	}
}

//Select only best extensions using forward weights
double FilterExtentionsDeep(Graph& g, BidirectionalPath& path, std::vector<EdgeId>& edges,
		PathLengths& lengths, PairedInfoIndices& pairedInfo, size_t edgesToExclude, bool forward,
		LoopDetector& detector,
		int depth = 1) {

	std::multimap<double, EdgeId> weights;
	std::vector<EdgeId> result;
	std::vector<int> distances;
	for (auto iter = edges.begin(); iter != edges.end(); ++iter) {
		double weight = 0;
		FindEdges(g, *iter, depth, result, distances, forward);

		for (int i = 0; i < (int) result.size(); ++i) {
			weight += ExtentionWeight(g, path, lengths, result[i], pairedInfo, edgesToExclude, forward, false, distances[i]);
		}

		weights.insert(std::make_pair(weight, *iter));
		detector.temp.weights[*iter] = weight;
	}

	DETAILED_INFO("Choosing weights deeper (" << depth << "): " << (forward ? "forward" : "backward"))
	for (auto iter = weights.begin(); iter != weights.end(); ++iter) {
		DETAILED_INFO(iter->second << " (" << g.length(iter->second) << ") = " << iter->first);
	}

	//Filling maximum edges
	edges.clear();
	auto bestEdge = weights.lower_bound((--weights.end())->first / lc_cfg::get().es.priority_coeff);
	for (auto maxEdge = bestEdge; maxEdge != weights.end(); ++maxEdge) {
		edges.push_back(maxEdge->second);
	}

	return bestEdge->first;
}

//Select only best extensions
double FilterExtentions(Graph& g, BidirectionalPath& path, std::vector<EdgeId>& edges,
		PathLengths& lengths, PairedInfoIndices& pairedInfo, size_t edgesToExclude, bool forward,
		LoopDetector& detector,
		bool useWeightFunction = false) {

	std::multimap<double, EdgeId> weights;

	for (auto iter = edges.begin(); iter != edges.end(); ++iter) {
		double weight = ExtentionWeight(g, path, lengths, *iter, pairedInfo, edgesToExclude, forward, useWeightFunction);
		weights.insert(std::make_pair(weight, *iter));
		detector.temp.AddAlternative(*iter, weight);
	}

	DETAILED_INFO("Choosing weights " << (forward ? "forward" : "backward"))
	for (auto iter = weights.begin(); iter != weights.end(); ++iter) {
		DETAILED_INFO(iter->second << " (" << g.length(iter->second) << ") = " << iter->first);
	}

	//Filling maximum edges
	edges.clear();
	auto bestEdge = weights.lower_bound((--weights.end())->first / lc_cfg::get().es.priority_coeff);
	for (auto maxEdge = bestEdge; maxEdge != weights.end(); ++maxEdge) {
		edges.push_back(maxEdge->second);
	}

	return bestEdge->first;
}

//Choose best matching extension
//Threshold to be discussed
EdgeId ChooseExtension(Graph& g, BidirectionalPath& path, std::vector<EdgeId>& edges,
		PathLengths& lengths, PairedInfoIndices& pairedInfo, double * maxWeight, size_t edgesToExclude, bool forward,
		LoopDetector& detector,
		PathStopHandler& handler) {

	detector.temp.clear();

	if (edges.size() == 0) {
		handler.AddStop(&path, NO_EXTENSION, forward);
		return 0;
	}

	if (edges.size() == 1) {
		detector.temp.AddAlternative(edges.back(), 1);
		return edges.back();
	}
	EdgeId toReturn = 0;
	if (lc_cfg::get().rs.research_mode && lc_cfg::get().rs.force_to_cycle) {
		for (auto edge = edges.begin(); edge != edges.end(); ++edge) {
			if (g.length(*edge) == lc_cfg::get().rs.cycle_priority_edge) {
				toReturn = *edge;
			}
		}
	}

	static bool useWeightFunctionFirst = lc_cfg::get().es.use_weight_function_first;

	if (useWeightFunctionFirst) {
		FilterExtentions(g, path, edges, lengths, pairedInfo, edgesToExclude, forward, detector, true);

		if (edges.size() == 1) {
			static double weightFunThreshold = lc_cfg::get().es.weight_fun_threshold;
			*maxWeight = ExtentionWeight(g, path, lengths, edges.back(), pairedInfo, edgesToExclude, forward);

			return toReturn == 0 ? ExtensionGoodEnough(edges.back(), *maxWeight, weightFunThreshold, g, path, handler, forward) : toReturn;
		}
	}

	*maxWeight = FilterExtentions(g, path, edges, lengths, pairedInfo, edgesToExclude, forward, detector);

	static double weightThreshold = lc_cfg::get().es.weight_threshold;
	if (edges.size() == 1) {
		return toReturn == 0 ? ExtensionGoodEnough(edges.back(), *maxWeight, weightThreshold, g, path, handler, forward) : toReturn;
	}
	else if (edges.size() > 1) {
		if (ExtensionGoodEnough(edges.back(), *maxWeight, weightThreshold) == 0) {
			DETAILED_INFO("No good extension");
			handler.AddStop(&path, NO_GOOD_EXTENSION, forward);
		} else {
			DETAILED_INFO("Cannot choose extension, no obvious maximum");

			static int maxDepth = lc_cfg::get().es.max_depth;
			for (int depth = 1; depth <= maxDepth; ++depth) {
				DETAILED_INFO("Trying to look deeper to " << depth);
				*maxWeight = FilterExtentionsDeep(g, path, edges, lengths, pairedInfo, edgesToExclude, forward, detector, depth);

				if (edges.size() == 1) {
					return toReturn == 0 ? ExtensionGoodEnough(edges.back(), *maxWeight, weightThreshold, g, path, handler, forward) : toReturn;
				}
			}
			INFO("Still no obvious selection, will stop growing");
			handler.AddStop(&path, MANY_GOOD_EXTENSIONS, forward);
		}
	}
	return toReturn;
}

//Count edges to be excluded
size_t EdgesToExcludeForward(Graph& g, BidirectionalPath& path, int from = -1) {
	static bool maxCycles = lc_cfg::get().ss.max_cycles;
	static LoopDetector detector;
	detector.clear();

	if (path.empty()) {
		return 0;
	}

	VertexId currentVertex = (from == -1) ? g.EdgeEnd(path.back()) : g.EdgeEnd(path[from]);
	size_t toExclude = 0;

	while (g.CheckUniqueIncomingEdge(currentVertex)) {
		EdgeId e = g.GetUniqueIncomingEdge(currentVertex);
		currentVertex = g.EdgeStart(e);
		++toExclude;

		detector.temp.clear();
		detector.temp.AddAlternative(e);
		detector.AddNewEdge(e, toExclude);
		if (CheckCycle(path, e, detector, maxCycles)) {
			INFO("Cycled trivial path");
			return 0;
		}
	}

	return std::min(toExclude, path.size());
}

//Count edges to be excludeD
size_t EdgesToExcludeBackward(Graph& g, BidirectionalPath& path, int from = -1) {
	static bool maxCycles = lc_cfg::get().ss.max_cycles;
	static LoopDetector detector;
	detector.clear();

	if (path.empty()) {
		return 0;
	}
	VertexId currentVertex = (from == -1) ? g.EdgeStart(path.front()) : g.EdgeStart(path[from]);
	size_t toExclude = 0;

	while (g.CheckUniqueOutgoingEdge(currentVertex)) {
		EdgeId e = g.GetUniqueOutgoingEdge(currentVertex);
		currentVertex = g.EdgeEnd(e);
		++toExclude;

		detector.temp.clear();
		detector.temp.AddAlternative(e);
		detector.AddNewEdge(e, toExclude);
		if (CheckCycle(path, e, detector, maxCycles)) {
			INFO("Cycled trivial path");
			return 0;
		}
	}

	return std::min(toExclude, path.size());
}

EdgeId FindExitFromLoop(BidirectionalPath& path, LoopDetector& detector, bool forward) {
	EdgeId lastEdge = forward ? path.back() : path.front();
	if (CountLoopExits(path, lastEdge, detector, forward) > 1) {
		DETAILED_INFO("Many exists, will resolve in normal way");
		return 0;
	}
	return FindFirstFork(path, lastEdge, detector, forward);
}

void ImitateFork(Graph& g, BidirectionalPath& path, PathLengths& lengths,
		LoopDetector& detector, PairedInfoIndices& pairedInfo,
		EdgeId loopEdge, EdgeId loopExit, bool forward, int excludeCycle = -1) {
	INFO("Imitating fork " << g.length(loopEdge) << " " << g.length(loopExit));
	size_t edgesToExclude = forward ? EdgesToExcludeForward(g, path) : EdgesToExcludeBackward(g, path);
	if (excludeCycle != -1) {
		edgesToExclude = excludeCycle;
	}

	detector.temp.clear();
	detector.temp.AddAlternative(loopExit, ExtentionWeight(g, path, lengths, loopExit, pairedInfo, edgesToExclude, forward));

	double w = ExtentionWeight(g, path, lengths, loopEdge, pairedInfo, edgesToExclude, forward);
	detector.temp.AddAlternative(loopEdge, w);
	detector.AddNewEdge(loopEdge, path.size(), w);
}

void RemoveFromDetector(LoopDetector& detector, EdgeId e, size_t iteration, EdgeId substitution = 0) {
	auto upper = --detector.data.upper_bound(e);
	auto lower = detector.data.upper_bound(e);

	for (auto iter = upper; iter != lower; --iter) {
		if (iter->second.iteration == iteration) {
			if (substitution == 0) {
				detector.data.erase(iter);
			} else {
				auto newData = std::make_pair(substitution, iter->second);
				detector.data.erase(iter);
				detector.data.insert(newData);
			}
			return;
		}
	}

	if (lower->second.iteration == iteration) {
		if (substitution == 0) {
			detector.data.erase(lower);
		} else {
			auto newData = std::make_pair(substitution, lower->second);
			detector.data.erase(lower);
			detector.data.insert(newData);
		}
		return;
	}

	DETAILED_INFO("Iteration not found in detector!");
}

void ReducePathTo(BidirectionalPath& path, LoopDetector& detector, size_t newSize, EdgeId loopExit, bool forward) {
	if (path.size() < newSize) {
		return;
	}

	if (forward) {
		while (path.size() > newSize + 1) {
			RemoveFromDetector(detector, path.back(), path.size() - 1);
			path.pop_back();
		}
		RemoveFromDetector(detector, path.back(), path.size() - 1, loopExit);
		path.pop_back();
	} else {
		while (path.size() > newSize + 1) {
			RemoveFromDetector(detector, path.front(), path.size() - 1);
			path.pop_front();
		}
		RemoveFromDetector(detector, path.front(), path.size() - 1, loopExit);
		path.pop_front();
	}
}

//Find length
bool CheckLoop(Graph& g, BidirectionalPath& path, LoopDetector& detector, EdgeId& loopEdge, size_t& loopLength, bool forward, size_t& loopSize) {
	loopLength = 0;
	if (loopEdge == 0) {
		//Loop already found
		DETAILED_INFO("Loop was already found");
		loopLength = CountLoopLength(g, path, detector, forward);

		if (PathIsOnlyLoop(path, detector, forward)) {
			DETAILED_INFO("Not enough info");
			return false;
		}

		loopEdge = FindExitFromLoop(path, detector, forward);
		if (loopEdge == 0) {
			DETAILED_INFO("Not found");
			return false;
		}
		loopSize = CountLoopEdges(forward ? path.back() : path.front(), detector);
	} else {
		loopLength = g.length(loopEdge) + g.length(forward ? path.back() : path.front());

		DETAILED_INFO("Short loop");
		if (PathIsOnlyLoop(path, loopEdge, forward)) {
			DETAILED_INFO("Not enough info");
			return false;
		}
		loopSize = 2;
	}
	return true;
}

bool MakeCorrectLoop(BidirectionalPath& path, LoopDetector& detector, EdgeId loopEdge, EdgeId loopExit,
		size_t originalSize, bool forward) {
	size_t properSize = GetMaxExitIteration(loopEdge, loopExit, detector, std::make_pair(originalSize - 1, path.size() - 1));
	size_t firstToExit = GetFirstExitIteration(loopEdge, loopExit, detector, std::make_pair(originalSize - 1, path.size() - 1));
//	if (firstToExit == std::numeric_limits<size_t>::max()) {
//		firstToExit = GetFirstExitIteration(loopEdge, loopExit, detector, 1);
//	}

	size_t loopLen = CountLoopEdges(forward ? path.back() : path.front(), detector);
	if (firstToExit == properSize) {
		DETAILED_INFO("Resolved fine: " << properSize << ", usual resolved: " << firstToExit << ", loop size " << loopLen);
	} else {
		INFO("Proper resolved: " << properSize << ", usual resolved: " << firstToExit << ", loop size " << loopLen);
	}

	if (properSize != 0) {
		ReducePathTo(path, detector, properSize, loopExit, forward);
	}
	else if (firstToExit != std::numeric_limits<size_t>::max()) {
		ReducePathTo(path, detector, firstToExit, loopExit, forward);
	}
	else {
		INFO("Cannot detect proper cycle exit!");
		ReducePathTo(path, detector, originalSize, 0, forward);
		return false;
	}

	if (forward) {
		path.push_back(loopExit);
	} else {
		path.push_front(loopExit);
	}
	return true;
}

//Find best loop path
bool ResolveLoopForward(Graph& g, BidirectionalPath& path, PathLengths& lengths,
		LoopDetector& detector, PairedInfoIndices& pairedInfo,
		EdgeId loopEdge) {

	DETAILED_INFO("Resolving loop forward");
	size_t originalSize = path.size();
	size_t loopLength = 0;
	size_t loopSize = 0;
	bool goodLoop = CheckLoop(g, path, detector, loopEdge, loopLength, true, loopSize);

	if (loopLength > GetMaxInsertSize(pairedInfo) - K) {
		DETAILED_INFO("Loop is too long");
		return !lc_cfg::get().lr.stop_on_long;
	}
	if (!goodLoop) {
		return true;
	}

	EdgeId loopExit = GetForwardFork(g, loopEdge);
	if (loopExit == 0) {
		return true;
	}
	size_t maxCycles = 2 * GetMaxInsertSize(pairedInfo) / loopLength + 2;

	size_t i = 0;
	do {
		ExtendTrivialForward(g, path, detector, &lengths);

		int excludeCycle = (lc_cfg::get().lr.exlude_cycle && loopSize == 2) ? loopSize * i + 1 : -1;
		ImitateFork(g, path, lengths, detector, pairedInfo, loopEdge, loopExit, true, excludeCycle);

		path.push_back(loopEdge);
		IncreaseLengths(g, lengths, loopEdge, true);

		++i;
	} while (i <= maxCycles && !LoopBecameStable(loopEdge, detector));

	detector.print(g);

	bool result = MakeCorrectLoop(path, detector, loopEdge, loopExit, originalSize, true);
	lengths.clear();
	RecountLengthsForward(g, path, lengths);
	DETAILED_INFO("Resolved");

	return result;
}

//Find best loop path
bool ResolveLoopBackward(Graph& g, BidirectionalPath& path, PathLengths& lengths,
		LoopDetector& detector, PairedInfoIndices& pairedInfo,
		EdgeId loopEdge) {

	DETAILED_INFO("Resolving loop backward");
	size_t originalSize = path.size();
	size_t loopLength = 0;
	size_t loopSize = 0;
	bool goodLoop = CheckLoop(g, path, detector, loopEdge, loopLength, false, loopSize);

	if (loopLength > GetMaxInsertSize(pairedInfo) - K) {
		DETAILED_INFO("Loop is too long");
		return !lc_cfg::get().lr.stop_on_long;
	}
	if (!goodLoop) {
		return true;
	}

	EdgeId loopExit = GetBackwardFork(g, loopEdge);
	if (loopExit == 0) {
		return true;
	}
	size_t maxCycles = 2 * GetMaxInsertSize(pairedInfo) / loopLength + 2;

	size_t i = 0;
	DETAILED_INFO("Imitating loop backward")
	do {
		INFO("Extending trivially backward")
		ExtendTrivialBackward(g, path, detector, &lengths);
		int excludeCycle = (lc_cfg::get().lr.exlude_cycle && loopSize == 2) ? loopSize * i + 1 : -1;
		ImitateFork(g, path, lengths, detector, pairedInfo, loopEdge, loopExit, false, excludeCycle);

		path.push_front(loopEdge);
		IncreaseLengths(g, lengths, loopEdge, false);

		DetailedPrintPath(g, path, lengths);

		++i;
	} while (i <= maxCycles && !LoopBecameStable(loopEdge, detector));

	detector.print(g);

	bool result = MakeCorrectLoop(path, detector, loopEdge, loopExit, originalSize, false);
	lengths.clear();
	RecountLengthsBackward(g, path, lengths);
	DETAILED_INFO("Resolved");

	return result;
}


//Extend path forward
bool ExtendPathForward(Graph& g, BidirectionalPath& path, PathLengths& lengths,
		LoopDetector& detector, PairedInfoIndices& pairedInfo,
		PathStopHandler& handler) {

	if (path.empty()) {
		return false;
	}
	double w = 0;
	static bool FULL_LOOP_REMOVAL = lc_cfg::get().lr.full_loop_removal;
	static size_t MAX_LOOPS = lc_cfg::get().lr.max_loops;
	static size_t LOOPS_TO_IVESTIGATE = lc_cfg::get().lr.loop_to_investigate;

	EdgeId loopEdge = 0;
	if (lc_cfg::get().lr.investigation) {
		loopEdge = IsEdgeInShortLoopForward(g, path.back());
		if (loopEdge != 0 || CheckCycle(path, path.back(), detector, LOOPS_TO_IVESTIGATE)) {
			DETAILED_INFO("Seed already near loop");
			if (!ResolveLoopForward(g, path, lengths, detector, pairedInfo, loopEdge)) {
				handler.AddStop(&path, LONG_LOOP, true);
				return false;
			}
			loopEdge = 0;
		}
	}

	std::vector<EdgeId> edges = g.OutgoingEdges(g.EdgeEnd(path.back()));
	EdgeId extension = ChooseExtension(g, path, edges, lengths, pairedInfo, &w, EdgesToExcludeForward(g, path), true, detector, handler);
	if (extension == 0) {
		return false;
	}

	detector.AddNewEdge(extension, path.size(), w);
	IncreaseLengths(g, lengths, extension, true);
	path.push_back(extension);

	DETAILED_INFO("Chosen forward " << extension << " (" << g.length(extension) << ")");
	DetailedPrintPath(g, path, lengths);

	if (lc_cfg::get().lr.investigation) {
		loopEdge = IsEdgeInShortLoopForward(g, extension);
		if (loopEdge != 0 || CheckCycle(path, extension, detector, LOOPS_TO_IVESTIGATE)) {
			if (!ResolveLoopForward(g, path, lengths, detector, pairedInfo, loopEdge)) {
				handler.AddStop(&path, LONG_LOOP, true);
				return false;
			}
		}
	}

	if (CheckCycle(path, extension, detector, MAX_LOOPS)) {
		detector.print(g);
		RemoveLoopForward(path, detector, FULL_LOOP_REMOVAL, MAX_LOOPS);

		DETAILED_INFO("Cycle detected");
		DetailedPrintPath(g, path, lengths);
		handler.AddStop(&path, LOOP, true);
		return false;
	}

	return true;
}

//And backward
bool ExtendPathBackward(Graph& g, BidirectionalPath& path, PathLengths& lengths,
		LoopDetector& detector, PairedInfoIndices& pairedInfo,
		PathStopHandler& handler) {

	if (path.empty()) {
		return false;
	}

	double w = 0;
	static bool FULL_LOOP_REMOVAL = lc_cfg::get().lr.full_loop_removal;
	static size_t MAX_LOOPS = lc_cfg::get().lr.max_loops;
	static size_t LOOPS_TO_IVESTIGATE = lc_cfg::get().lr.loop_to_investigate;

	EdgeId loopEdge = 0;
	if (lc_cfg::get().lr.investigation) {
		loopEdge = IsEdgeInShortLoopBackward(g, path.front());
		if (loopEdge != 0 || CheckCycle(path, path.front(), detector, LOOPS_TO_IVESTIGATE)) {
			DETAILED_INFO("Seed already near loop");
			if (!ResolveLoopBackward(g, path, lengths, detector, pairedInfo, loopEdge)){
				handler.AddStop(&path, LONG_LOOP, false);
				return false;
			}
			loopEdge = 0;
		}
	}

	std::vector<EdgeId> edges = g.IncomingEdges(g.EdgeStart(path.front()));
	EdgeId extension = ChooseExtension(g, path, edges, lengths, pairedInfo, &w, EdgesToExcludeBackward(g, path), false, detector, handler);
	if (extension == 0) {
		return false;
	}

	detector.AddNewEdge(extension, path.size(), w);
	IncreaseLengths(g, lengths, extension, false);
	path.push_front(extension);

	DETAILED_INFO("Chosen backward " << extension << " (" << g.length(extension) << ")");
	DetailedPrintPath(g, path, lengths);

	if (lc_cfg::get().lr.investigation) {
		loopEdge = IsEdgeInShortLoopBackward(g, extension);
		if (loopEdge != 0 || CheckCycle(path, extension, detector, LOOPS_TO_IVESTIGATE)) {
			if (!ResolveLoopBackward(g, path, lengths, detector, pairedInfo, loopEdge)) {
				handler.AddStop(&path, LONG_LOOP, false);
				return false;
			}
		}
	}

	if (CheckCycle(path, extension, detector, MAX_LOOPS)) {
		detector.print(g);
		RemoveLoopBackward(path, detector, FULL_LOOP_REMOVAL, MAX_LOOPS);

		DETAILED_INFO("Cycle detected");
		DetailedPrintPath(g, path, lengths);
		handler.AddStop(&path, LOOP, false);
		return false;
	}

	return true;
}

void RecountDetectorForward(Graph& g, BidirectionalPath& path, PairedInfoIndices& pairedInfo, LoopDetector& detector) {
	BidirectionalPath emulPath;
	PathLengths emulLengths;
	detector.clear();

	DETAILED_INFO("Recounting detector forward");

	for (int i = 0; i < (int) path.size(); ++i) {
		DETAILED_INFO(i);
		size_t edgesToExclude = EdgesToExcludeForward(g, emulPath);

		detector.temp.clear();
		if (g.OutgoingEdgeCount(g.EdgeStart(path[i])) == 1) {
			detector.temp.AddAlternative(path[i]);
			detector.AddNewEdge(path[i], i);
		}
		else {
			auto edges = g.OutgoingEdges(g.EdgeStart(path[i]));

			for (auto iter = edges.begin(); iter != edges.end(); ++iter) {
				double weight = (i == 0 || (int) edgesToExclude >= i) ?
						1.0 : ExtentionWeight(g, emulPath, emulLengths, *iter, pairedInfo, edgesToExclude, true, false);

				detector.temp.AddAlternative(*iter, weight);
			}
			detector.AddNewEdge(path[i], i, detector.temp.weights[path[i]]);
		}

		emulPath.push_back(path[i]);
		IncreaseLengths(g, emulLengths, path[i], true);
	}
}

void RecountDetectorBackward(Graph& g, BidirectionalPath& path, PairedInfoIndices& pairedInfo, LoopDetector& detector) {
	BidirectionalPath emulPath;
	PathLengths emulLengths;
	detector.clear();

	DETAILED_INFO("Recounting detector backward");

	for (int i = path.size() - 1; i >= 0; --i) {
		size_t edgesToExclude = EdgesToExcludeBackward(g, emulPath);

		detector.temp.clear();
		if (g.IncomingEdgeCount(g.EdgeEnd(path[i])) == 1) {
			detector.temp.AddAlternative(path[i]);
			detector.AddNewEdge(path[i], path.size() - 1 - i);
		}
		else {
			auto edges = g.IncomingEdges(g.EdgeEnd(path[i]));

			for (auto iter = edges.begin(); iter != edges.end(); ++iter) {
				double weight = (i == (int) path.size() - 1 || (int) edgesToExclude >= (int) path.size() - 1 - i) ?
						1.0 : ExtentionWeight(g, emulPath, emulLengths, *iter, pairedInfo, edgesToExclude, false, false);

				detector.temp.AddAlternative(*iter, weight);
			}

			detector.AddNewEdge(path[i], path.size() - 1 - i, detector.temp.weights[path[i]]);
		}

		emulPath.push_front(path[i]);
		IncreaseLengths(g, emulLengths, path[i], false);
	}
}

//Grow selected seed in both directions
void GrowSeed(Graph& g, BidirectionalPath& seed, PairedInfoIndices& pairedInfo, PathStopHandler& handler) {
	PathLengths lengths;
	LoopDetector detector;

	static size_t maxIS = GetMaxInsertSize(pairedInfo);
	int i = 0;
	bool stop = false;

	while (i < lc_cfg::get().es.max_iter && !stop) {
		RecountLengthsForward(g, seed, lengths);

		DETAILED_INFO("Before forward");
		DetailedPrintPath(g, seed, lengths);

		RecountDetectorForward(g, seed, pairedInfo, detector);
		while (ExtendPathForward(g, seed, lengths, detector, pairedInfo, handler)) {
		}

		if (PathLength(g, seed) > maxIS) {
			stop = true;
		}

		RecountLengthsBackward(g, seed, lengths);
		DETAILED_INFO("Before backward");
		DetailedPrintPath(g, seed, lengths);

		RecountDetectorBackward(g, seed, pairedInfo, detector);
		while (ExtendPathBackward(g, seed, lengths, detector, pairedInfo, handler)) {
		}

		++i;
	}
}

//Metrics for choosing seeds
size_t SeedPriority(const BidirectionalPath& seed) {
	return seed.size();
}

//Find paths with given seeds
void FindPaths(Graph& g, std::vector<BidirectionalPath>& seeds, PairedInfoIndices& pairedInfo, std::vector<BidirectionalPath>& paths,
		PathStopHandler& handler) {
	std::multimap<size_t, BidirectionalPath*, std::greater<size_t> > priorityQueue;
	static bool ALL_SEEDS = lc_cfg::get().sc.all_seeds;
	static double EDGE_COVERAGE_TRESHOLD = lc_cfg::get().sc.edge_coverage;
	static double LENGTH_COVERAGE_TRESHOLD = lc_cfg::get().sc.len_coverage;

	INFO("Finding paths started");
	for(auto seed = seeds.begin(); seed != seeds.end(); ++seed) {
		priorityQueue.insert(std::make_pair(SeedPriority(*seed), &(*seed)));
	}

	for(auto seed = priorityQueue.rbegin(); seed != priorityQueue.rend(); ++seed) {
		GrowSeed(g, *(seed->second), pairedInfo, handler);
		paths.push_back(*(seed->second));

		if (!ALL_SEEDS && PathsCoverage(g, paths) > EDGE_COVERAGE_TRESHOLD && PathsLengthCoverage(g, paths) > LENGTH_COVERAGE_TRESHOLD) {
			break;
		}
	}

	INFO("Finding paths finished");
}

// === Totally symmertic mode ===

void CompareConjugateGrowth(Graph& g, BidirectionalPath& path, PathLengths& lengths, LoopDetector& detector,
		BidirectionalPath& conjPath, PathLengths& conjLengths, LoopDetector& conjDetector) {

	INFO("After symmetric growth")
	if (!ComparePaths(path, conjPath)) {
		PrintPath(g, path, lengths);
		detector.print(g);
		PrintPath(g, conjPath, conjLengths);
		conjDetector.print(g);

	} else {
		INFO("Paths are equal!")
	}

}

void SymmetrizePaths() {

}

//Grow selected seed in both directions
void GrowSeedSymmetric(Graph& g, BidirectionalPath& seed, BidirectionalPath& conjSeed,
		PairedInfoIndices& pairedInfo, PathStopHandler& handler) {

	PathLengths lengths;
	LoopDetector detector;
	PathLengths conjLengths;
	LoopDetector conjDetector;

	static size_t maxIS = GetMaxInsertSize(pairedInfo);
	int i = 0;
	bool stop = false;
	bool start = true;

	while (i < lc_cfg::get().es.max_iter && !stop) {
		if (!start || lc_cfg::get().first_grow_forward) {
			RecountLengthsForward(g, seed, lengths);
			RecountLengthsBackward(g, conjSeed, conjLengths);

			DETAILED_INFO("Before forward");
			DetailedPrintPath(g, seed, lengths);
			DetailedPrintPath(g, conjSeed, conjLengths);

			RecountDetectorForward(g, seed, pairedInfo, detector);
			RecountDetectorBackward(g, conjSeed, pairedInfo, conjDetector);

			while (ExtendPathForward(g, seed, lengths, detector, pairedInfo, handler)) {
			}
			while (ExtendPathBackward(g, conjSeed, conjLengths, conjDetector, pairedInfo, handler)) {
			}

			if (PathLength(g, seed) > maxIS) {
				stop = true;
			}

		}
		start = false;

		RecountLengthsBackward(g, seed, lengths);
		RecountLengthsForward(g, conjSeed, conjLengths);

		DETAILED_INFO("Before backward");
		DetailedPrintPath(g, seed, lengths);
		DetailedPrintPath(g, conjSeed, conjLengths);

		RecountDetectorBackward(g, seed, pairedInfo, detector);
		RecountDetectorForward(g, conjSeed, pairedInfo, conjDetector);

		while (ExtendPathBackward(g, seed, lengths, detector, pairedInfo, handler)) {
		}
		while (ExtendPathForward(g, conjSeed, conjLengths, conjDetector, pairedInfo, handler)) {
		}

		++i;
	}
}

void FindPathsSymmetric(Graph& g, std::vector<BidirectionalPath>& seeds, PairedInfoIndices& pairedInfo,
		PathStopHandler& handler, std::vector<int>& seedPairs) {


	INFO("Finding paths started in totally symmetric way");
	std::set<int> grown;
	for(int i = 0; i < (int) seeds.size(); ++i) {
		if (grown.count(i) == 0) {
			GrowSeedSymmetric(g, seeds[i], seeds[seedPairs[i]], pairedInfo, handler);
			grown.insert(i);
			grown.insert(seedPairs[i]);
		}
	}

	INFO("Finding paths finished");
}

} // namespace long_contigs


#endif /* PATHS_HPP_ */
