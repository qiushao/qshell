local ir = {}
ir.__index = ir -- 将 __index 指向自身，实现继承/方法查找
ir.keycode = {
    power = 0xC0,
    input = 0xC1,
    up = 0xC2,
    down = 0xC3,
    left = 0xC4,
    right = 0xC5,
    ok = 0xC6,
    menu = 0xC7,
    home = 0xC8,
    back = 0xC9,
    vol_plus = 0xCA,
    vol_minus = 0xCB,
    mute = 0xCC,
    video = 0xCD,
    favorite = 0xCE,
    more = 0xCF
}

-- 静态方法（点号语法）
-- 构造函数
function ir.new(dev, options)
    local self = setmetatable({}, ir) -- 创建实例并设置元表
    self.dev = dev
    self.options = options
    self.serial = qshell.serial.open(dev, options)
    return self
end

-- 实例方法（注意冒号语法，隐含 self 参数）
-- 点击按键
function ir:click(keycode)
    self.serial:writeBinary(keycode)
end

-- 随机生成一个按键值
function ir:randomKey()
    local keys = {}
    for _, keycode in pairs(self.keycode) do
        keys[#keys + 1] = keycode
    end
    return keys[math.random(#keys)]
end

function ir:release()
    self.serial:close()
end

return ir
