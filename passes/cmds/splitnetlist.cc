#include "kernel/sigtools.h"
#include "kernel/yosys.h"
#include <set>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// Recursively traverses backward from a sig, record if a cell was traversed, and push onto the cell's inputs.
// Similarly with assign statements traverses lhs -> rhs
void recordTransFanin(RTLIL::SigSpec &sig, dict<RTLIL::SigSpec, std::set<Cell *> *> &sig2CellsInFanin,
		      dict<RTLIL::SigSpec, RTLIL::SigSpec> &lhsSig2RhsSig, std::set<Cell *> &visitedCells, std::set<RTLIL::SigSpec> &visitedSigSpec)
{
	if (sig.is_fully_const()) {
		return;
	}
	if (visitedSigSpec.count(sig)) {
		return;
	}
	visitedSigSpec.insert(sig);
	if (sig2CellsInFanin.count(sig)) {
		std::set<Cell *> *sigFanin = sig2CellsInFanin[sig];
		for (std::set<Cell *>::iterator it = sigFanin->begin(); it != sigFanin->end(); it++) {
			Cell *cell = *it;
			if (visitedCells.count(cell)) {
				continue;
			}
			visitedCells.insert(cell);
			for (auto &conn : cell->connections()) {
				IdString portName = conn.first;
				RTLIL::SigSpec actual = conn.second;

				if (cell->input(portName)) {
					if (!actual.is_chunk()) {
						for (auto it = actual.chunks().rbegin(); it != actual.chunks().rend(); ++it) {
							RTLIL::SigSpec sub_actual = *it;
							recordTransFanin(sub_actual, sig2CellsInFanin, lhsSig2RhsSig, visitedCells, visitedSigSpec);
						}
					} else {
						recordTransFanin(actual, sig2CellsInFanin, lhsSig2RhsSig, visitedCells, visitedSigSpec);
					}
				}
			}
		}
	}
	if (lhsSig2RhsSig.count(sig)) {
		RTLIL::SigSpec rhs = lhsSig2RhsSig[sig];
		recordTransFanin(rhs, sig2CellsInFanin, lhsSig2RhsSig, visitedCells, visitedSigSpec);
	}
}

// Signal cell driver(s), precompute a cell output signal to a cell map
void sigCellDrivers(RTLIL::Design *design, dict<RTLIL::SigSpec, std::set<Cell *> *> &sig2CellsInFanin)
{
	for (auto cell : design->top_module()->cells()) {
		for (auto &conn : cell->connections()) {
			IdString portName = conn.first;
			RTLIL::SigSpec actual = conn.second;
			std::set<Cell *> *newSet;
			if (cell->output(portName)) {
				if (!actual.is_chunk()) {
					for (auto it = actual.chunks().rbegin(); it != actual.chunks().rend(); ++it) {
						RTLIL::SigSpec sub_actual = *it;
						if (sig2CellsInFanin.count(sub_actual)) {
							newSet = sig2CellsInFanin[sub_actual];
						} else {
							newSet = new std::set<Cell *>;
							sig2CellsInFanin[sub_actual] = newSet;
						}
						newSet->insert(cell);
					}
				} else {
					if (sig2CellsInFanin.count(actual)) {
						newSet = sig2CellsInFanin[actual];
					} else {
						newSet = new std::set<Cell *>;
						sig2CellsInFanin[actual] = newSet;
					}
					newSet->insert(cell);
				}
			}
		}
	}
}

// Assign statements fanin, traces the lhs to rhs sigspecs and precompute a map
void lhs2rhs(RTLIL::Design *design, dict<RTLIL::SigSpec, RTLIL::SigSpec> &lhsSig2rhsSig)
{
	for (auto it = design->top_module()->connections().begin(); it != design->top_module()->connections().end(); ++it) {
		RTLIL::SigSpec lhs = it->first;
		RTLIL::SigSpec rhs = it->second;
		if (rhs.is_fully_const()) {
			continue;
		}
		if (!lhs.is_chunk()) {
			if (lhs.chunks().size() != rhs.chunks().size()) {
				auto rit = rhs.chunks().rbegin();
				long unsigned rhsSize = 0;
				while (rit != rhs.chunks().rend()) {
					RTLIL::SigSpec sub_rhs = *rit;
					if (sub_rhs.is_fully_const()) {
						rhsSize += (sub_rhs.as_chunk()).width;
					} else {
						rhsSize++;
					}
					rit++;
				}
				if (lhs.chunks().size() != rhsSize) {
					continue;
				}
			}
			auto lit = lhs.chunks().rbegin();
			auto rit = rhs.chunks().rbegin();
			while (rit != rhs.chunks().rend()) {
				RTLIL::SigSpec sub_lhs = *lit;
				RTLIL::SigSpec sub_rhs = *rit;
				if (sub_rhs.is_fully_const()) {
					int constSize = (sub_rhs.as_chunk()).width;
					while (constSize--) {
						lit++;
					}
					rit++;
					continue;
				}
				lhsSig2rhsSig[sub_lhs] = sub_rhs;
				lit++;
				rit++;
			}
		} else {
			lhsSig2rhsSig[lhs] = rhs;
		}
	}
}

std::string_view rtrim_until(std::string_view str, char c)
{
	auto pos = str.rfind(c);
	if (pos != std::string_view::npos)
		str = str.substr(0, pos);
	return str;
}

std::string id2String(RTLIL::IdString internal_id)
{
	const char *str = internal_id.c_str();
	std::string result = str;
	return result;
}

std::string replaceAll(std::string_view str, std::string_view from, std::string_view to)
{
	size_t start_pos = 0;
	std::string result(str);
	while ((start_pos = result.find(from, start_pos)) != std::string::npos) {
		result.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substr of 'from'
	}
	return result;
}

struct SplitNetlist : public ScriptPass {
	SplitNetlist()
	    : ScriptPass("splitnetlist", "Splits a netlist into multiple modules using transitive fanin grouping. \
	       The output names that belong in the same logical cluster have to have the same prefix: <prefix>_<name>")
	{
	}
	void script() {}

	void execute(std::vector<std::string>, RTLIL::Design *design) override
	{
		if (design == nullptr) {
			log_error("No design object");
			return;
		}
		// Precompute cell output sigspec to cell map
		dict<RTLIL::SigSpec, std::set<Cell *> *> sig2CellsInFanin;
		sigCellDrivers(design, sig2CellsInFanin);
		// Precompute lhs to rhs sigspec map
		dict<RTLIL::SigSpec, RTLIL::SigSpec> lhsSig2RhsSig;
		lhs2rhs(design, lhsSig2RhsSig);
		// Struct representing a cluster
		typedef struct CellsAndSigs {
			std::set<Cell *> visitedCells;
			std::set<RTLIL::SigSpec> visitedSigSpec;
		} CellsAndSigs;
		// Cluster mapped by prefix
		typedef std::map<std::string, CellsAndSigs> CellName_ObjectMap;
		CellName_ObjectMap cellName_ObjectMap;
		// Record logic cone by output sharing the same prefix
		for (auto wire : design->top_module()->wires()) {
			if (!wire->port_output)
				continue;
			std::string output_port_name = wire->name.c_str();
			std::string_view po_prefix = rtrim_until(std::string_view(output_port_name), '_');
			std::set<Cell *> visitedCells;
			std::set<RTLIL::SigSpec> visitedSigSpec;
			RTLIL::SigSpec actual = wire;
			// Visit the output sigspec
			recordTransFanin(actual, sig2CellsInFanin, lhsSig2RhsSig, visitedCells, visitedSigSpec);
			// Visit the output sigspec bits
			for (int i = 0; i < actual.size(); i++) {
				SigSpec bit_sig = actual.extract(i, 1);
				recordTransFanin(bit_sig, sig2CellsInFanin, lhsSig2RhsSig, visitedCells, visitedSigSpec);
			}
			// Record the visited objects in the corresponding cluster
			CellName_ObjectMap::iterator itr = cellName_ObjectMap.find(std::string(po_prefix));
			if (itr == cellName_ObjectMap.end()) {
				CellsAndSigs components;
				for (auto cell : visitedCells) {
					components.visitedCells.insert(cell);
				}
				for (auto sig : visitedSigSpec) {
					components.visitedSigSpec.insert(sig);
				}
				cellName_ObjectMap.emplace(std::string(po_prefix), components);
			} else {
				CellsAndSigs &components = itr->second;
				for (auto cell : visitedCells) {
					components.visitedCells.insert(cell);
				}
				for (auto sig : visitedSigSpec) {
					components.visitedSigSpec.insert(sig);
				}
			}
		}
		// Create submod attributes for the submod command
		for (CellName_ObjectMap::iterator itr = cellName_ObjectMap.begin(); itr != cellName_ObjectMap.end(); itr++) {
			// std::cout << "Cluster name: " << itr->first << std::endl;
			CellsAndSigs &components = itr->second;
			for (auto cell : components.visitedCells) {
				cell->set_string_attribute(RTLIL::escape_id("submod"), itr->first);
				// std::cout << "  CELL: " << cell->name.c_str() << std::endl;
			}
			// for (auto sigspec : components.visitedSigSpec) {
			//  std::cout << "  SIG: " << SigName(sigspec) << std::endl;
			// }
			// std::cout << std::endl;
		}
		// Execute the submod command
		Pass::call(design, "submod -copy");
	}
} SplitNetlist;

PRIVATE_NAMESPACE_END
