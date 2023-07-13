function H = contour_mfem_gf(ax, mesh, filename, varargin)
args = varargin;
% args = {};
% mesh = read_mfem_mesh('mesh000561.mesh');
% filename = 'rho000253.gf';

dim = mesh.dim;
fid = fopen(filename, 'r');
str = textscan(fid, '%s', 'Delimiter', '\n');
str = str{1};
fes = str{2}(strfind(str{2}, ': ') + 2 : end);
if ~startsWith(fes, 'L2')
    error('Only L2 function is supported');
end
fes = fes(4:end);
if ~startsWith(fes, 'T1')
    error('Only Lgendre-Gauss-Lobatto basis is supported');
end
fes = fes(4:end);
if ~startsWith(fes, num2str(mesh.dim))
    error('Input solution and mesh dimension do not agree');
end
order = str2double(fes(end));
data = str2double(str(6:end));
data = reshape(data, [], mesh.nrE);

if dim == 2
    if size(mesh.v4e, 1) == 3
        data = processData2DTri(order, data);
        h = tricontourfh(ax, mesh, data, args{:});
    elseif size(mesh.v4e, 1) == 4
        data = processData2DRect(order, data);

        h = tricontourfh(ax, mesh, data, args{:});
    else
        error('For 2D, mesh should be triangular or rectangular');
    end
else
    error('Dimension must be 2D');
end
if nargout
    H = h;
end
end

function data = processData2DTri(order, data)

end
function data = processData2DRect(order, data)
submesh = rect2rectmesh(-1, 1, -1, 1, 1, 1);
[rtri, stri] = SNodes2D(order*2);
r1D = SNodes1D(order);
intpV = vertexInterp2D(rtri, stri);
X = intpV*submesh.x4tri; X = X(:);
Y = intpV*submesh.y4tri; Y = Y(:);
intpBx = basisInterp1D(r1D, X, order);
intpBy = basisInterp1D(r1D, Y, order);
intpB = repmat(intpBx, 1, length(r1D));
for i = 1 : length(r1D)
    intpB(:, length(r1D)*(i-1) + 1 : length(r1D)*i) = ...
        intpB(:, length(r1D)*(i-1) + 1 : length(r1D)*i).*intpBy(:,i);
end
data = intpB*data;
data = reshape(data, length(rtri), []);

end
