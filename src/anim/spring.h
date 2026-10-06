// Ressort amorti (intégration semi-implicite à sous-pas fixes).
#pragma once

namespace md {

class Spring {
public:
    explicit Spring(double stiffness = 420, double damping = 38);
    void setParams(double stiffness, double damping);
    void setTarget(double t) { target_ = t; }
    void snap(double v);                 // valeur = cible = v, vitesse nulle
    bool step(double dt);                // true tant que le ressort bouge
    double value() const { return value_; }
    double target() const { return target_; }
    double velocity() const { return velocity_; }
    bool settled() const;

private:
    double stiffness_, damping_;
    double value_ = 0, target_ = 0, velocity_ = 0;
};

} // namespace md
