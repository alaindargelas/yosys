/*
 *  yosys -- Yosys Open SYnthesis Suite
 *
 *  Copyright (C) 2012  Claire Xenia Wolf <claire@yosyshq.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */
#ifdef VERIFIC_LINEFILE_INCLUDES_LOOPS
/*
    This Visitor decorates the AST with a loop ID attribute for all outer for loops.
    All AST nodes contained within the subtree of an outer for-loop
    have the same ID carried as an additional payload of the "linefile" struct.
    The ID is unique accross the flat RTL module set, as it is computed before elaboration. 
    It is not unique per instance of the modules.
    A further separation of cells belonging to a given loop instance is necessary by means of 
    connectivity analysis. 
    No "loop instance" information seems to exist to cluster those loops elements together unfortunately.
*/
class DecorateLoopsVisitor : public VeriVisitor
{
      public:
	DecorateLoopsVisitor() : VeriVisitor() {};
	~DecorateLoopsVisitor() {};
	virtual void VERI_VISIT(VeriLoop, node)
	{
		//std::cout << "Loop in: " << (VeriLoop *)&node << " id: " << outerLoopId << std::endl;
		if (loopStack.empty()) {
            // We increase the loop count when we enter a new set of imbricated loops,
            // That way we have a loop index for the outermost loop as we want to identify and group 
            // logic generated by imbricated loops
			outerLoopId++;
		}
		loopStack.push((VeriLoop *)&node);
	}

	void PreAction(VeriTreeNode &/*node*/)
	{
		//VeriNode *vnode = dynamic_cast<VeriNode *>(&node);
		//std::cout << "Node pre: " << vnode << std::endl;
	}

	virtual void PostAction(VeriTreeNode &node)
	{
		//std::cout << "Node post: " << (VeriTreeNode *)&node << std::endl;
		if (loopStack.size()) {
			if (loopStack.top() == (VeriLoop *)&node) {
				loopStack.pop();
				std::cout << "Loop out: " << (VeriFor *)&node << std::endl;
				return;
			}
			Verific::linefile_type linefile = (Verific::linefile_type)node.Linefile();
            // Unfortunately there is no good way to systematically copy certain AST attributes to the Netlist attributes like:
            //VeriNode *vnode = dynamic_cast<VeriNode *>(&node);
			//vnode->AddAttribute(" in_loop", new VeriIntVal(outerLoopId));
            // Instead using linefile struct to pass that information:
            if (linefile)
				linefile->SetInLoop(outerLoopId);
		}
	}

      private:
	std::stack<VeriLoop *> loopStack;
	uint32_t outerLoopId = 0;
};
#endif