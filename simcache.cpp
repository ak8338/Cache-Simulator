
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <regex>
#include <cstdlib>
#include <limits>

using namespace std;

// Some helpful constant values that we'll be using.
size_t const static NUM_REGS = 8;
size_t const static MEM_SIZE = 1<<13;
size_t const static REG_SIZE = 1<<16;


/*
    Prints out the correctly-formatted configuration of a cache.

    @param cache_name The name of the cache. "L1" or "L2"

    @param size The total size of the cache, measured in memory cells.
        Excludes metadata

    @param assoc The associativity of the cache. One of [1,2,4,8,16]

    @param blocksize The blocksize of the cache. One of [1,2,4,8,16,32,64])

    @param num_rows The number of rows in the given cache.
*/
void print_cache_config(const string &cache_name, int size, int assoc, int blocksize, int num_rows) {
    cout << "Cache " << cache_name << " has size " << size <<
        ", associativity " << assoc << ", blocksize " << blocksize <<
        ", rows " << num_rows << endl;
}

/*
    Prints out a correctly-formatted log entry.

    @param cache_name The name of the cache where the event
        occurred. "L1" or "L2"

    @param status The kind of cache event. "SW", "HIT", or
        "MISS"

    @param pc The program counter of the memory
        access instruction

    @param addr The memory address being accessed.

    @param row The cache row or set number where the data
        is stored.
*/
void print_log_entry(const string &cache_name, const string &status, int pc, int addr, int row) {
    cout << left << setw(8) << cache_name + " " + status <<  right <<
        " pc:" << setw(5) << pc <<
        "\taddr:" << setw(5) << addr <<
        "\trow:" << setw(4) << row << endl;
}

//struct for blocks
struct CacheBlock {
    bool valid;
    uint16_t tag; 
    int lru;  

    CacheBlock(int blocksize) : valid(false), tag(0), lru(0) {}
};

//struct for row
struct CacheRow {
    vector<CacheBlock> blocks;

    CacheRow(int associativity, int blocksize) {
        blocks.reserve(associativity);
        for (int i = 0; i < associativity; ++i) {
            blocks.emplace_back(blocksize);
        }
    }
};

//struct for cache that consists of cachesize, associativity and blocksize. it also calculates the 
//number rows. Functions loadword and storeword handle the cache simulation for sw and lw.
struct Cache {
    std::vector<CacheRow> rows;
    int cacheSize;
    int associativity;
    int blocksize;
    int numRows;

    Cache(int size, int assoc, int bsize) :
        cacheSize(size), associativity(assoc), blocksize(bsize),
        numRows(size / (assoc * bsize)) {
        rows.reserve(numRows);
        for (int i = 0; i < numRows; ++i) {
            rows.emplace_back(associativity, blocksize);
        }
    }

    int loadWord(uint16_t address,  int &rowIndex) {
        uint16_t tag = (address / blocksize) / numRows;
        rowIndex = (address / blocksize) % numRows;
        CacheRow &row = rows[rowIndex];
        

        //check for hits and update LRU
        for (int i = 0; i < associativity; ++i) {
            row.blocks[i].lru++;  
            if (row.blocks[i].valid && row.blocks[i].tag == tag) {
                
                row.blocks[i].lru = 0;  
                for (int j = 0; j < associativity; ++j) {
                    if (j != i) row.blocks[j].lru++;  
                }
                return 1; // Hit
            }
        }

        // check for Miss
        int lruIndex = -1;
        int maxLRU = -1;
        for (int i = 0; i < associativity; ++i) {
            if (row.blocks[i].lru > maxLRU) {
                lruIndex = i;
                maxLRU = row.blocks[i].lru;
            }
        }

        CacheBlock &replaceBlock = row.blocks[lruIndex];
        replaceBlock.valid = true;
        replaceBlock.tag = tag;
        replaceBlock.lru = 0; 
        

        return 0; // Miss
    }

    void storeWord(uint16_t address, int &rowIndex) {
        uint16_t tag = (address / blocksize) / numRows;
        rowIndex = (address / blocksize) % numRows;
        CacheRow &row = rows[rowIndex];
        int lruIndex = -1;
        int maxLRU = -1;

        for (int i = 0; i < associativity; i++) {
            if (!row.blocks[i].valid || row.blocks[i].lru > maxLRU) {
                lruIndex = i;
                maxLRU = row.blocks[i].lru;
            }
            row.blocks[i].lru++;
        }

        CacheBlock &block = row.blocks[lruIndex];
        block.valid = true;
        block.tag = tag;
    
        block.lru = 0;  
    }
};

/*
    Loads an E20 machine code file into the list
    provided by mem. We assume that mem is
    large enough to hold the values in the machine
    code file.

    @param f Open file to read from
    @param mem Array represetnting memory into which to read program
*/
void load_machine_code(ifstream &f, uint16_t mem[]) {
    regex machine_code_re("^ram\\[(\\d+)\\] = 16'b(\\d+);.*$");
    size_t expectedaddr = 0;
    string line;
    while (getline(f, line)) {
        smatch sm;
        if (!regex_match(line, sm, machine_code_re)) {
            cerr << "Can't parse line: " << line << endl;
            exit(1);
        }
        size_t addr = stoi(sm[1], nullptr, 10);
        unsigned instr = stoi(sm[2], nullptr, 2);
        if (addr != expectedaddr) {
            cerr << "Memory addresses encountered out of sequence: " << addr << endl;
            exit(1);
        }
        if (addr >= MEM_SIZE) {
            cerr << "Program too big for memory" << endl;
            exit(1);
        }
        expectedaddr ++;
        mem[addr] = instr;
    }
}

/*
    Prints the current state of the simulator, including
    the current program counter, the current register values,
    and the first memquantity elements of memory.

    @param pc The final value of the program counter
    @param regs Final value of all registers
    @param memory Final value of memory
    @param memquantity How many words of memory to dump
*/
void print_state(uint16_t pc, uint16_t regs[], uint16_t memory[], size_t memquantity) {
    cout << setfill(' ');
    cout << "Final state:" << endl;
    cout << "\tpc=" <<setw(5)<< pc << endl;

    for (size_t reg=0; reg<NUM_REGS; reg++)
        cout << "\t$" << reg << "="<<setw(5)<<regs[reg]<<endl;

    cout << setfill('0');
    bool cr = false;
    for (size_t count=0; count<memquantity; count++) {
        cout << hex << setw(4) << memory[count] << " ";
        cr = true;
        if (count % 8 == 7) {
            cout << endl;
            cr = false;
        }
    }
    if (cr)
        cout << endl;
}

//Simulates the instructions for add,sub,or,and,slt and jr
void execute_instruction(uint16_t instr, uint16_t regs[], uint16_t& pc) {
    unsigned functionCode = instr & 0xF; 
    
    unsigned regSrcA = (instr >> 10) & 0x7; 
    unsigned regSrcB = (instr >> 7) & 0x7;  
    unsigned regDst = (instr >> 4) & 0x7;  
    const unsigned functionCodeAdd = 0b0000;  
    const unsigned functionCodeSub = 0b0001;  
    const unsigned functionCodeOr = 0b0010;  
    const unsigned functionCodeAnd = 0b0011;  
    const unsigned functionCodeSlt = 0b0100;  
    const unsigned functionCodeJr = 0b1000;  
 

    switch (functionCode) {
        case functionCodeAdd: 
            regs[regDst] = regs[regSrcA] + regs[regSrcB];
            break;
        case functionCodeSub: 
            regs[regDst] = regs[regSrcA] - regs[regSrcB];
            break;
        case functionCodeOr: 
            regs[regDst] = regs[regSrcA] | regs[regSrcB];
            break;
        case functionCodeAnd: 
            regs[regDst] = regs[regSrcA] & regs[regSrcB];
            break;
        case functionCodeSlt: 
            regs[regDst] = (regs[regSrcA] < regs[regSrcB]) ? 1 : 0;
            break;
        case functionCodeJr: 
            pc = regs[(instr >> 10) & 0x7] - 1; 
            break;
        default:
            
            break;
    }
    regs[0] = 0;

    pc += 1; 
}

//simulates instruction for  slti lw sw jeq and addi
void execute_imm_instruction(uint16_t instr, uint16_t regs[], uint16_t& pc, uint16_t memory[],  Cache& l1Cache, Cache* l2Cache = nullptr) {
    uint16_t  opcode = (instr >> 13) & 0x7; 
    uint16_t  regSrc = (instr >> 10) & 0x7; 
    uint16_t  regDst = (instr >> 7) & 0x7; 
    uint16_t imm = instr & 0x7F; 
    const uint16_t  opcodeSlti = 0b111; 
    const uint16_t  opcodeLw = 0b100;   
    const uint16_t opcodeSw = 0b101;  
    const uint16_t  opcodeJeq = 0b110; 
    const uint16_t  opcodeAddi = 0b001; 
    
    if (imm & 0x40) { 
        imm |= 0xFF80; 
    }

    uint16_t address = (regs[regSrc] + imm) & 0x1FFF;  
    int l1Row, l2Row;

    switch (opcode) {
        case opcodeSlti: { 
            regs[regDst] = (static_cast<unsigned>(regs[regSrc]) < static_cast<unsigned>(imm)) ? 1 : 0;
            pc += 1;
            break;
        }
        case opcodeLw: { 
            uint16_t regAddr = (instr >> 10) & 0x7; 
            uint16_t regDst = (instr >> 7) & 0x7; 
            uint16_t imm = instr & 0x7F; 

            if (imm & 0x40) imm |= 0xFF80;
            uint16_t address = (regs[regAddr] + imm) & 0x1FFF;  

            bool hit = l1Cache.loadWord(address,  l1Row);
            print_log_entry("L1", (hit ? "HIT" : "MISS"), pc, address, l1Row);

            if (!hit && l2Cache) {  
                hit = l2Cache->loadWord(address, l2Row);
                print_log_entry("L2", (hit ? "HIT" : "MISS"), pc, address, l2Row);
            }

            if (!hit) {  
                uint16_t loadAddr = (regs[regAddr] + imm) & 8191; 
                if (loadAddr < (1 << 13)) { 
                    regs[regDst] = memory[loadAddr]; 
                }
            }
            pc += 1; 
            break;
        } 

        case opcodeSw: { 
            uint16_t regAddr = (instr >> 10) & 0x7; 
            uint16_t regSrc = (instr >> 7) & 0x7; 
            uint16_t imm = instr & 0x7F; 
           
            if (imm & 0x40) imm |= 0xFF80;

             uint16_t storeAddr = (regs[regAddr] + imm) & 8191; 
            if (storeAddr < (1 << 13)) { 
                memory[storeAddr] = regs[regSrc]; 
                }

            l1Cache.storeWord(address,  l1Row);
            print_log_entry("L1", "SW", pc, address, l1Row);
            if (l2Cache) {
                l2Cache->storeWord(address,  l2Row);
                print_log_entry("L2", "SW", pc, address, l2Row);
            }

            pc += 1; 
            break;
        }
        case opcodeJeq: { 
            if (regs[regSrc] == regs[regDst]) { 
                pc = pc + 1 + imm; 
            } else {
                pc += 1; 
            }
            break;
        }
        case opcodeAddi: { 
            regs[regDst] = regs[regSrc] + imm; 
            pc += 1;
            break;
        }
    
        default: {
        
            pc += 1; 
            break;
        }
    }
    regs[0] = 0;
}


//simulates instructions for j and jal
void execute_control_instruction(uint16_t instr, uint16_t regs[], uint16_t& pc, bool& isHalt) {
    uint16_t opcode = (instr >> 13) & 0x7; 
    uint16_t imm = instr & 0x1FFF; 
    const uint16_t opcodeJ = 0b010;
    const uint16_t opcodeJal = 0b011; 
    
    switch (opcode) {
        case opcodeJ: 
            if (imm == pc) { 
                isHalt = true; 
            } else {
                pc = imm; 
            }
            break;
        case opcodeJal:
            regs[7] = pc + 1; 
            pc = imm; 
            break;
       
        default:
            
            break;
    }
    regs[0] = 0;
}


/**
    Main function
    Takes command-line args as documented below
*/
int main(int argc, char *argv[]) {
    /*
        Parse the command-line arguments
    */
    char *filename = nullptr;
    bool do_help = false;
    bool arg_error = false;
    string cache_config;
    for (int i=1; i<argc; i++) {
        string arg(argv[i]);
        if (arg.rfind("-",0)==0) {
            if (arg== "-h" || arg == "--help")
                do_help = true;
            else if (arg=="--cache") {
                i++;
                if (i>=argc)
                    arg_error = true;
                else
                    cache_config = argv[i];
            }
            else
                arg_error = true;
        } else {
            if (filename == nullptr)
                filename = argv[i];
            else
                arg_error = true;
        }
    }
    /* Display error message if appropriate */
    if (arg_error || do_help || filename == nullptr) {
        cerr << "usage " << argv[0] << " [-h] [--cache CACHE] filename" << endl << endl;
        cerr << "Simulate E20 cache" << endl << endl;
        cerr << "positional arguments:" << endl;
        cerr << "  filename    The file containing machine code, typically with .bin suffix" << endl<<endl;
        cerr << "optional arguments:"<<endl;
        cerr << "  -h, --help  show this help message and exit"<<endl;
        cerr << "  --cache CACHE  Cache configuration: size,associativity,blocksize (for one"<<endl;
        cerr << "                 cache) or"<<endl;
        cerr << "                 size,associativity,blocksize,size,associativity,blocksize"<<endl;
        cerr << "                 (for two caches)"<<endl;
        return 1;
    }


    /* parse cache config */
    if (cache_config.size() > 0) {
        vector<int> parts;
        size_t pos;
        size_t lastpos = 0;
        while ((pos = cache_config.find(",", lastpos)) != string::npos) {
            parts.push_back(stoi(cache_config.substr(lastpos,pos)));
            lastpos = pos + 1;
        }
        parts.push_back(stoi(cache_config.substr(lastpos)));
        if (parts.size() == 3) {
            int L1size = parts[0];
            int L1assoc = parts[1];
            int L1blocksize = parts[2];

            // TODO: execute E20 program and simulate one cache here
            Cache l1Cache(parts[0], parts[1], parts[2]);
            print_cache_config("L1", L1size, L1assoc, L1blocksize, l1Cache.numRows);
            
            uint16_t memory[MEM_SIZE] = {0}; 
            uint16_t regs[NUM_REGS] = {0}; 
            uint16_t pc = 0;
            bool isHalt = false;

            ifstream f(filename);
            if (!f.is_open()) {
                cerr << "Can't open file "<<filename<<endl;
            return 1;
            }
            //Load f and parse using load_machine_code
            load_machine_code(f, memory); // Load the machine code into memory
            f.close();

            // E20 simulation.
            while (!isHalt) { // Simulation loop
                uint16_t instr = memory[pc & 8191]; 
                uint16_t imm = instr & 0x1FFF; 
                uint16_t opcode = (instr >> 13) & 0x7; 
                const uint16_t opcodeFirst = 0b000;
                const uint16_t opcodeSlti = 0b111; 
                const uint16_t opcodeLw = 0b100;   
                const uint16_t opcodeSw = 0b101;  
                const uint16_t opcodeJeq = 0b110; 
                const uint16_t opcodeAddi = 0b001; 
                const uint16_t opcodeJ = 0b010; 
                const uint16_t opcodeJal = 0b011;
        
                if (opcode == opcodeJ && imm == pc) {
                    isHalt = true; 
                continue; 
                }
                if (opcode == opcodeFirst) {
                    execute_instruction(instr, regs, pc); 
                } else if (opcode == opcodeSlti || opcode == opcodeAddi || opcode == opcodeLw || opcode == opcodeSw || opcode == opcodeJeq){
                    execute_imm_instruction(instr, regs, pc, memory, l1Cache); 
                } else if (opcode == opcodeJ || opcode == opcodeJal) {
                    execute_control_instruction(instr, regs, pc, isHalt); 
                }
        
            }
        } else if (parts.size() == 6) {
            int L1size = parts[0];
            int L1assoc = parts[1];
            int L1blocksize = parts[2];
            int L2size = parts[3];
            int L2assoc = parts[4];
            int L2blocksize = parts[5];
    
            // TODO: execute E20 program and simulate two caches here
            Cache l1Cache(parts[0], parts[1], parts[2]);
            Cache *l2Cache = nullptr;
            l2Cache = new Cache(L2size, L2assoc, L2blocksize);
            print_cache_config("L1", L1size, L1assoc, L1blocksize, l1Cache.numRows);
            print_cache_config("L2", L2size, L2assoc, L2blocksize, l2Cache->numRows);
            
            uint16_t memory[MEM_SIZE] = {0}; 
            uint16_t regs[NUM_REGS] = {0}; 
            uint16_t pc = 0;
            bool isHalt = false;

            ifstream f(filename);
            if (!f.is_open()) {
                cerr << "Can't open file "<<filename<<endl;
            return 1;
            }
            //Load f and parse using load_machine_code
            load_machine_code(f, memory); // Load the machine code into memory
            f.close();

            //E20 simulation.
            while (!isHalt) { 
                uint16_t instr = memory[pc & 8191]; 
                uint16_t imm = instr & 0x1FFF; 
                uint16_t opcode = (instr >> 13) & 0x7; 
                const uint16_t opcodeFirst = 0b000;
                const uint16_t opcodeSlti = 0b111; 
                const uint16_t opcodeLw = 0b100;   
                const uint16_t opcodeSw = 0b101;  
                const uint16_t opcodeJeq = 0b110; 
                const uint16_t opcodeAddi = 0b001; 
                const uint16_t opcodeJ = 0b010; 
                const uint16_t opcodeJal = 0b011;
        
                if (opcode == opcodeJ && imm == pc) {
                    isHalt = true; 
                continue; 
                }
                if (opcode == opcodeFirst) {
                    execute_instruction(instr, regs, pc); 
                } else if (opcode == opcodeSlti || opcode == opcodeAddi || opcode == opcodeLw || opcode == opcodeSw || opcode == opcodeJeq){
                    execute_imm_instruction(instr, regs, pc, memory, l1Cache, l2Cache); 
                } else if (opcode == opcodeJ || opcode == opcodeJal) {
                    execute_control_instruction(instr, regs, pc, isHalt); 
                }      
            }
            if (l2Cache) {
            delete l2Cache; 
            }
    //print_state(pc, regs, memory, 128); 
        } else {
            cerr << "Invalid cache config"  << endl;
            return 1;
        }
    }

    return 0;
}