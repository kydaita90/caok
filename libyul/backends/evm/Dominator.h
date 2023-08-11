
/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * Dominator analysis of a control flow graph.
 * The implementation is based on the following paper:
 * https://www.cs.princeton.edu/courses/archive/spr03/cs423/download/dominators.pdf
 * See appendix B pg. 139.
 */
#pragma once

#include <libyul/backends/evm/ControlFlowGraph.h>
#include <libsolutil/Visitor.h>

#include <deque>
#include <map>
#include <vector>
#include <set>

namespace solidity::yul
{

template<typename Vertex, typename ForEachSuccessor>
class Dominator
{
public:

	Dominator(Vertex const& _entry, size_t _numVertices):
		m_vertex(_numVertices),
		m_immediateDominator(lengauerTarjanDominator(_entry, _numVertices))
	{
		buildDominatorTree();
	}

	std::vector<Vertex> const& vertices() const
	{
		return m_vertex;
	}

	std::map<Vertex, size_t> const& vertexIndices() const
	{
		return m_vertexIndex;
	}

	std::vector<size_t> const& immediateDominators() const
	{
		return m_immediateDominator;
	}

	std::map<size_t, std::vector<size_t>> const& dominatorTree() const
	{
		return m_dominatorTree;
	}

	// Checks whether ``_a`` dominates ``_b`` by going
	// through the path from ``_b`` to the entry node.
	// If ``_a`` is found, then it dominates ``_b``
	// otherwise it doesn't.
	bool dominates(Vertex const& _a, Vertex const& _b)
	{
		size_t aIdx = m_vertexIndex[_a];
		size_t bIdx = m_vertexIndex[_b];

		if (aIdx == bIdx)
			return true;

		size_t idomIdx = m_immediateDominator[bIdx];
		while (idomIdx != 0)
		{
			if (idomIdx == aIdx)
				return true;
			idomIdx = m_immediateDominator[idomIdx];
		}
		// Now that we reach the entry node (i.e. idx = 0),
		// either ``aIdx == 0`` or it does not dominates other node.
		return idomIdx == aIdx;
	}

	// Find all dominators of a node _v
	// @note for a vertex ``_v``, the _v’s inclusion in the set of dominators of ``_v`` is implicit.
	std::vector<Vertex> dominatorsOf(Vertex const& _v)
	{
		solAssert(!m_vertex.empty());
		// The entry node always dominates all other nodes
		std::vector<Vertex> dominators{m_vertex[0]};

		size_t idomIdx = m_immediateDominator[m_vertexIndex[_v]];
		if (idomIdx == 0)
			return dominators;

		while (idomIdx != 0)
		{
			dominators.emplace_back(m_vertex[idomIdx]);
			idomIdx = m_immediateDominator[idomIdx];
		}
		return dominators;
	}

	void buildDominatorTree()
	{
		solAssert(!m_vertex.empty());
		solAssert(!m_immediateDominator.empty());

		//Ignoring the entry node since no one dominates it.
		for (size_t i = 1; i < m_immediateDominator.size(); ++i)
			m_dominatorTree[m_immediateDominator[i]].emplace_back(i);
	}

	// Path compression updates the ancestors of vertices along
	// the path to the ancestor with the minimum label value.
	void compressPath(
		std::vector<size_t> &_ancestor,
		std::vector<size_t> &_label,
		std::vector<size_t> &_semi,
		size_t _v
	)
	{
		solAssert(_ancestor[_v] != std::numeric_limits<size_t>::max());
		size_t u = _ancestor[_v];
		if (_ancestor[u] != std::numeric_limits<size_t>::max())
		{
			compressPath(_ancestor, _label, _semi, u);
			if (_semi[_label[u]] < _semi[_label[_v]])
				_label[_v] = _label[u];
			_ancestor[_v] = _ancestor[u];
		}
	}

	std::vector<size_t> lengauerTarjanDominator(Vertex const& _entry, size_t numVertices)
	{
		solAssert(numVertices > 0);
		// semi(w): The dfs index of the semidominator of ``w``.
		std::vector<size_t> semi(numVertices, std::numeric_limits<size_t>::max());
		// parent(w): The index of the vertex which is the parent of ``w`` in the spanning
		// tree generated by the dfs.
		std::vector<size_t> parent(numVertices, std::numeric_limits<size_t>::max());
		// ancestor(w): The highest ancestor of a vertex ``w`` in the dominator tree used
		// for path compression.
		std::vector<size_t> ancestor(numVertices, std::numeric_limits<size_t>::max());
		// label(w): The index of the vertex ``w`` with the minimum semidominator in the path
		// to its parent.
		std::vector<size_t> label(numVertices, 0);

		// ``link`` adds an edge to the virtual forest.
		// It copies the parent of w to the ancestor array to limit the search path upwards.
		// TODO: implement sophisticated link-eval algorithm as shown in pg 132
		// See: https://www.cs.princeton.edu/courses/archive/spr03/cs423/download/dominators.pdf
		auto link = [&](size_t _parent, size_t _w)
		{
			ancestor[_w] = _parent;
		};

		// ``eval`` computes the path compression.
		// Finds ancestor with lowest semi-dominator dfs number (i.e. index).
		auto eval = [&](size_t _v) -> size_t
		{
			if (ancestor[_v] != std::numeric_limits<size_t>::max())
			{
				compressPath(ancestor, label, semi, _v);
				return label[_v];
			}
			return _v;
		};

		// step 1
		std::set<Vertex> visited;
		// predecessors(w): The set of vertices ``v`` such that (``v``, ``w``) is an edge of the graph.
		std::vector<std::set<size_t>> predecessors(numVertices);
		// bucket(w): a set of vertices whose semidominator is ``w``
		// The index of the array represents the vertex's ``dfIdx``
		std::vector<std::deque<size_t>> bucket(numVertices);
		// idom(w): the index of the immediate dominator of ``w``
		std::vector<size_t> idom(numVertices, std::numeric_limits<size_t>::max());
		// The number of vertices reached during the dfs.
		// The vertices are indexed based on this number.
		size_t dfIdx = 0;
		auto dfs = [&](Vertex const& _v, auto _dfs) -> void {
			if (visited.count(_v))
				return;
			visited.insert(_v);
			m_vertex[dfIdx] = _v;
			m_vertexIndex[_v] = dfIdx;
			semi[dfIdx] = dfIdx;
			label[dfIdx] = dfIdx;
			dfIdx++;
			ForEachSuccessor{}(_v, [&](Vertex const& w) {
				if (semi[dfIdx] == std::numeric_limits<size_t>::max())
				{
					parent[dfIdx] = m_vertexIndex[_v];
					_dfs(w, _dfs);
				}
				predecessors[m_vertexIndex[w]].insert(m_vertexIndex[_v]);
			});
		};
		dfs(_entry, dfs);

		// Process the vertices in decreasing order of the dfs number
		for (auto it = m_vertex.rbegin(); it != m_vertex.rend(); ++it)
		{
			Vertex const& w = m_vertexIndex[*it];
			// step 3
			// NOTE: this is an optimization, i.e. performing the step 3 before step 2.
			// The goal is to process the bucket in the beginning of the loop for the vertex ``w``
			// instead of ``parent[w]`` in the end of the loop as described in the original paper.
			// Inverting those steps ensures that a bucket is only processed once and
			// it does not need to be erased.
			// The optimization proposal is available here: https://jgaa.info/accepted/2006/GeorgiadisTarjanWerneck2006.10.1.pdf pg.77
			for_each(
				bucket[w].begin(),
				bucket[w].end(),
				[&](size_t v)
				{
					size_t u = eval(v);
					idom[v] = (semi[u] < semi[v]) ? u : w;
				}
			);

			// step 2
			for (size_t v: predecessors[w])
			{
				size_t u = eval(v);
				if (semi[u] < semi[w])
					semi[w] = semi[u];
			}
			bucket[semi[w]].emplace_back(w);
			link(parent[w], w);
		}

		// step 4
		idom[0] = 0;
		for (auto it = m_vertex.begin() + 1; it != m_vertex.end(); ++it)
		{
			size_t w = m_vertexIndex[*it];
			if (idom[w] != semi[w])
				idom[w] = idom[idom[w]];
		}
		return idom;
	}
private:
	// Keep the list of vertices in the dfs order.
	// i.e. m_vertex[i]: the vertex whose dfs index is i.
	std::vector<Vertex> m_vertex;
	// Maps Vertex to their dfs index.
	std::map<Vertex, size_t> m_vertexIndex;
	// Immediate dominators by index.
	// Maps a Vertex based on its dfs index (i.e. array index) to its immediate dominator dfs index.
	//
	// e.g. to get the immediate dominator of a Vertex w:
	// idomIdx = m_immediateDominator[m_vertexIndex[w]]
	// idomVertex = m_vertex[domIdx]
	std::vector<size_t> m_immediateDominator;

	// Maps a Vertex to all vertices that it dominates.
	// If the vertex does not dominates any other vertex it has no entry in the map.
	std::map<size_t, std::vector<size_t>> m_dominatorTree;
};
}
