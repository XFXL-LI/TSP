#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <map>

class System {
private:

    System();
    ~System();

public:
    static System& getInstance() {
        static System instance;
        return instance;
    }
    void SystemInit(void);
    void SystemSerialInit(void);
    void SystemConfigInit(void);
    void SystemSetupInit(void);

    void SystemTaskInit(void);

    System(const System&) = delete;
    System& operator=(const System&) = delete;
};


#endif // SYSTEM_H