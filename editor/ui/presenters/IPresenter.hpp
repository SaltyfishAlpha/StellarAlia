#pragma once

namespace StellarAlia::Editor {

class IPresenter {
public:
    virtual ~IPresenter() = default;
    virtual void Update(float dt) = 0;
};

} // namespace StellarAlia::Editor
