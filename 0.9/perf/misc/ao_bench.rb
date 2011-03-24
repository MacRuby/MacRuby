module AOBench
  # AO render benchmark 
  # Original program (C) Syoyo Fujita in Javascript (and other languages)
  #      http://lucille.atso-net.jp/blog/?p=642
  #      http://lucille.atso-net.jp/blog/?p=711
  # Ruby(yarv2llvm) version by Hideki Miura
  #      http://github.com/miura1729/yarv2llvm/blob/a888d8ce6855e70b630a8673d4cfe075a8e44f0e/sample/ao-render.rb
  # Modified by Tomoyuki Chikanaga
  #
  
  IMAGE_WIDTH = 64   # original value: 256
  IMAGE_HEIGHT = 64  # original value: 256
  NSUBSAMPLES = 2
  NAO_SAMPLES = 6    # original value: 8
  
  class Vec
    def initialize(x, y, z)
      @x = x
      @y = y
      @z = z
    end
  
    attr_accessor :x, :y, :z
  
    def vadd(b)
      Vec.new(@x + b.x, @y + b.y, @z + b.z)
    end
  
    def vsub(b)
      Vec.new(@x - b.x, @y - b.y, @z - b.z)
    end
  
    def vcross(b)
      Vec.new(@y * b.z - @z * b.y,
              @z * b.x - @x * b.z,
              @x * b.y - @y * b.x)
    end
  
    def vdot(b)
      @x * b.x + @y * b.y + @z * b.z
    end
  
    def vlength
      Math.sqrt(@x * @x + @y * @y + @z * @z)
    end
  
    def vnormalize!
      len = vlength
      if len > 1.0e-17
        r_len = 1.0 / len
        @x *= r_len
        @y *= r_len
        @z *= r_len
      end
      self
    end
  end
  
  class Sphere
    def initialize(center, radius)
      @center = center
      @radius_2 = radius * radius
    end
  
    def intersect(ray, isect)
      rs = ray.org.vsub(@center)
      b = rs.vdot(ray.dir)
      c = rs.vdot(rs) - @radius_2
      d = b * b - c
      if d > 0.0
        t = - b - Math.sqrt(d)
  
        isect.cross(self, ray, t)
      end
      nil
    end
  
    def normal_vec(pos)
      pos.vsub(@center).vnormalize!
    end
  end
  
  class Plane
    def initialize(p, n)
      @p = p
      @n = n
    end
  
    def intersect(ray, isect)
      d = -@p.vdot(@n)
      v = ray.dir.vdot(@n)
      v0 = v
      if v < 0.0
        v0 = -v
      end
      if v0 < 1.0e-17
        return
      end
  
      t = -(ray.org.vdot(@n) + d) / v
  
      isect.cross(self, ray, t)
      nil
    end
  
    def normal_vec(pos)
      @n
    end
  end
  
  class Ray
    def initialize(org, dir)
      @org = org
      @dir = dir
    end
    attr_reader :org, :dir
  end
  
  class Isect
    def initialize
      @t = Float::MAX
      @hit = false
      @pl = Vec.new(0.0, 0.0, 0.0)
      @normal = Vec.new(0.0, 0.0, 0.0)
    end
  
    attr_reader :normal
  
    def hit?
      @hit
    end
  
    def cross(geom, ray, dist)
      if 0.0 < dist and dist < @t
        @hit = true
        @t = dist
        @pl = Vec.new(ray.org.x + ray.dir.x * dist,
                      ray.org.y + ray.dir.y * dist,
                      ray.org.z + ray.dir.z * dist)
        @normal = geom.normal_vec(@pl)
      end
      nil
    end
  
    def surface(eps)
      Vec.new(@pl.x + eps * @normal.x,
              @pl.y + eps * @normal.y,
              @pl.z + eps * @normal.z)
    end
  end
  
  def self.clamp(f)
    i = f * 255.5
    if i > 255.0
      i = 255.0
    end
    if i < 0.0
      i = 0.0
    end
    i.round
  end
  
  def self.otherBasis(basis, n)
    basis[2] = Vec.new(n.x, n.y, n.z)
    basis[1] = Vec.new(0.0, 0.0, 0.0)
    
    if n.x < 0.6 and n.x > -0.6
      basis[1].x = 1.0
    elsif n.y < 0.6 and n.y > -0.6
      basis[1].y = 1.0
    elsif n.z < 0.6 and n.z > -0.6
      basis[1].z = 1.0
    else
      basis[1].x = 1.0
    end
  
    basis[0] = basis[1].vcross(basis[2])
    basis[0].vnormalize!
  
    basis[1] = basis[2].vcross(basis[0])
    basis[1].vnormalize!
  end
  
  class Scene
    def initialize
      @spheres = Array.new
      @spheres[0] = Sphere.new(Vec.new(-2.0, 0.0, -3.5), 0.5)
      @spheres[1] = Sphere.new(Vec.new(-0.5, 0.0, -3.0), 0.5)
      @spheres[2] = Sphere.new(Vec.new(1.0, 0.0, -2.2), 0.5)
      @plane = Plane.new(Vec.new(0.0, -0.5, 0.0), Vec.new(0.0, 1.0, 0.0))
    end
  
    def ambient_occlusion(isect)
      basis = Array.new
      AOBench.otherBasis(basis, isect.normal)
  
      ntheta    = NAO_SAMPLES
      nphi      = NAO_SAMPLES
      eps       = 0.0001
      occlusion = 0.0
  
      p0 = isect.surface(eps)
      nphi.times do |j|
        ntheta.times do |i|
          r = rand
          rr = Math.sqrt(1.0 - r)
          phi = 2.0 * Math::PI * rand
          x = Math.cos(phi) * rr
          y = Math.sin(phi) * rr
          z = Math.sqrt(r)
  
          rx = x * basis[0].x + y * basis[1].x + z * basis[2].x
          ry = x * basis[0].y + y * basis[1].y + z * basis[2].y
          rz = x * basis[0].z + y * basis[1].z + z * basis[2].z
  
          raydir = Vec.new(rx, ry, rz)
          ray = Ray.new(p0, raydir)
  
          occisect = Isect.new
          @spheres[0].intersect(ray, occisect)
          @spheres[1].intersect(ray, occisect)
          @spheres[2].intersect(ray, occisect)
          @plane.intersect(ray, occisect)
          if occisect.hit?
            occlusion = occlusion + 1.0
          else
            0.0
          end
        end
      end
  
      occlusion = (ntheta * nphi - occlusion).to_f / (ntheta * nphi)
  
      occlusion
    end
  
    def render(w, h, nsubsamples)
      cnt = 0
      pixbuf = []
      nsf = nsubsamples.to_f
      nsf_2 = nsf * nsf
      h.times do |y|
        w.times do |x|
          rad = 0.0
  
          # Subsmpling
          nsubsamples.times do |v|
            nsubsamples.times do |u|
  
              cnt = cnt + 1
              wf = w.to_f
              hf = h.to_f
              xf = x.to_f
              yf = y.to_f
              uf = u.to_f
              vf = v.to_f
  
              px = (xf + (uf / nsf) - (wf / 2.0)) / (wf / 2.0)
              py = -(yf + (vf / nsf) - (hf / 2.0)) / (hf / 2.0)
  
              eye = Vec.new(px, py, -1.0)
              eye.vnormalize!
  
              ray = Ray.new(Vec.new(0.0, 0.0, 0.0), eye)
  
              isect = Isect.new
              @spheres[0].intersect(ray, isect)
              @spheres[1].intersect(ray, isect)
              @spheres[2].intersect(ray, isect)
              @plane.intersect(ray, isect)
              if isect.hit?
                rad += ambient_occlusion(isect)
              end
            end
          end
  
          pixbuf << AOBench.clamp(rad / nsf_2)
        end
      end
    end
  end

  def self.test
    Scene.new.render(IMAGE_WIDTH, IMAGE_HEIGHT, NSUBSAMPLES)
  end
end

perf_test('ao_bench') { AOBench.test }
